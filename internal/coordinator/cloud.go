package coordinator

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"log/slog"
	"os"
	"strings"
	"sync"
	"time"

	"cloud.google.com/go/run/apiv2"
	"cloud.google.com/go/run/apiv2/runpb"
	"cloud.google.com/go/storage"
	"google.golang.org/api/iterator"
)

// CloudManager launches one Cloud Run Job execution per task and tracks executions for cancel.
type CloudManager interface {
	LaunchTask(ctx context.Context, workerType string, taskID string, env map[string]string) error
	CancelTask(ctx context.Context, taskID string) error
	ProvisionStorage(ctx context.Context, bucketName string) error
	GCSObjectHash(ctx context.Context, gcsURI string) (string, error)
	GCSObjectExists(ctx context.Context, gcsURI string) (bool, error)
	WriteSentinel(ctx context.Context, gcsURI string) error
}

// CloudRunManager triggers pre-defined Cloud Run Jobs with per-task environment overrides.
type CloudRunManager struct {
	jobsClient *run.JobsClient
	execClient *run.ExecutionsClient

	skewerJobName string
	loomJobName   string

	storageClient *storage.Client

	mu              sync.Mutex
	executionByTask map[string]string // task ID -> Cloud Run Execution resource name
}

// NewCloudRunManager builds a manager using Application Default Credentials.
// Set CLOUD_RUN_SKEWER_JOB and CLOUD_RUN_LOOM_JOB to full resource names, e.g.
// projects/PROJECT_ID/locations/us-central1/jobs/skewer-cloud-worker
func NewCloudRunManager(ctx context.Context) (*CloudRunManager, error) {
	jobsClient, err := run.NewJobsClient(ctx)
	if err != nil {
		return nil, fmt.Errorf("run Jobs client: %w", err)
	}
	execClient, err := run.NewExecutionsClient(ctx)
	if err != nil {
		_ = jobsClient.Close()
		return nil, fmt.Errorf("run Executions client: %w", err)
	}

	storageClient, err := storage.NewClient(ctx)
	if err != nil {
		_ = jobsClient.Close()
		_ = execClient.Close()
		return nil, fmt.Errorf("storage client: %w", err)
	}

	return &CloudRunManager{
		jobsClient:      jobsClient,
		execClient:      execClient,
		skewerJobName:   os.Getenv("CLOUD_RUN_SKEWER_JOB"),
		loomJobName:     os.Getenv("CLOUD_RUN_LOOM_JOB"),
		storageClient:   storageClient,
		executionByTask: make(map[string]string),
	}, nil
}

func (c *CloudRunManager) Close() error {
	var firstErr error
	if c.jobsClient != nil {
		if err := c.jobsClient.Close(); err != nil {
			firstErr = err
		}
	}
	if c.execClient != nil {
		if err := c.execClient.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	if c.storageClient != nil {
		if err := c.storageClient.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	return firstErr
}

func (c *CloudRunManager) jobNameForWorkerType(workerType string) (string, error) {
	switch workerType {
	case "skewer", "skewer-worker":
		if c.skewerJobName == "" {
			return "", fmt.Errorf("CLOUD_RUN_SKEWER_JOB is not set")
		}
		return c.skewerJobName, nil
	case "loom", "loom-worker":
		if c.loomJobName == "" {
			return "", fmt.Errorf("CLOUD_RUN_LOOM_JOB is not set")
		}
		return c.loomJobName, nil
	default:
		return "", fmt.Errorf("unknown worker type %q", workerType)
	}
}

func envMapToRunPB(m map[string]string) []*runpb.EnvVar {
	out := make([]*runpb.EnvVar, 0, len(m))
	for k, v := range m {
		out = append(out, &runpb.EnvVar{
			Name: k,
			Values: &runpb.EnvVar_Value{
				Value: v,
			},
		})
	}
	return out
}

// LaunchTask starts a Cloud Run Job execution with the given environment overrides.
func (c *CloudRunManager) LaunchTask(ctx context.Context, workerType string, taskID string, env map[string]string) error {
	jobName, err := c.jobNameForWorkerType(workerType)
	if err != nil {
		return err
	}

	req := &runpb.RunJobRequest{
		Name: jobName,
		Overrides: &runpb.RunJobRequest_Overrides{
			ContainerOverrides: []*runpb.RunJobRequest_Overrides_ContainerOverride{
				{
					Env: envMapToRunPB(env),
				},
			},
		},
	}

	op, err := c.jobsClient.RunJob(ctx, req)
	if err != nil {
		return fmt.Errorf("RunJob: %w", err)
	}

	c.trackRunJobOperation(op, taskID)
	slog.Info("launched cloud run job execution", "task_id", taskID, "job", jobName)
	return nil
}

func (c *CloudRunManager) trackRunJobOperation(op *run.RunJobOperation, taskID string) {
	go func() {
		bg, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
		defer cancel()
		for {
			select {
			case <-bg.Done():
				return
			default:
			}
			ex, err := op.Poll(bg)
			if err != nil {
				slog.Warn("RunJob poll failed", "task_id", taskID, "error", err)
				return
			}
			if meta, err := op.Metadata(); err == nil && meta != nil && meta.GetName() != "" {
				c.mu.Lock()
				c.executionByTask[taskID] = meta.GetName()
				c.mu.Unlock()
				return
			}
			if op.Done() && ex != nil && ex.GetName() != "" {
				c.mu.Lock()
				c.executionByTask[taskID] = ex.GetName()
				c.mu.Unlock()
				return
			}
			time.Sleep(200 * time.Millisecond)
		}
	}()
}

// CancelTask deletes the Cloud Run execution for a task, if known.
func (c *CloudRunManager) CancelTask(ctx context.Context, taskID string) error {
	c.mu.Lock()
	name, ok := c.executionByTask[taskID]
	delete(c.executionByTask, taskID)
	c.mu.Unlock()
	if !ok || name == "" {
		slog.Debug("cancel task: no execution mapped yet", "task_id", taskID)
		return nil
	}

	op, err := c.execClient.DeleteExecution(ctx, &runpb.DeleteExecutionRequest{Name: name})
	if err != nil {
		return fmt.Errorf("DeleteExecution: %w", err)
	}
	go func() {
		_, _ = op.Wait(ctx)
	}()
	return nil
}

// ProvisionStorage verifies access to the GCS bucket.
func (c *CloudRunManager) ProvisionStorage(ctx context.Context, bucketName string) error {
	if c.storageClient == nil {
		return fmt.Errorf("GCP storage client not initialized")
	}
	slog.Info("verifying GCS bucket access", "bucket", bucketName)
	bucket := c.storageClient.Bucket(bucketName)
	attrs, err := bucket.Attrs(ctx)
	if err != nil {
		return fmt.Errorf("access user bucket (gs://%s): %w", bucketName, err)
	}
	slog.Info("GCS bucket verified", "bucket", attrs.Name, "location", attrs.Location)
	return nil
}

// parseGCSURI splits a gs://bucket/path URI into (bucket, object).
func parseGCSURI(uri string) (bucket, object string, err error) {
	if !strings.HasPrefix(uri, "gs://") {
		return "", "", fmt.Errorf("not a gs:// URI: %s", uri)
	}
	trimmed := strings.TrimPrefix(uri, "gs://")
	sep := strings.IndexByte(trimmed, '/')
	if sep < 0 {
		return trimmed, "", nil
	}
	return trimmed[:sep], trimmed[sep+1:], nil
}

// GCSObjectHash returns a SHA-256 hex digest of the object's raw content.
// Uses the object's MD5 from GCS metadata when available to avoid a full download;
// falls back to streaming the content if the object has no stored MD5 (e.g. composite objects).
func (c *CloudRunManager) GCSObjectHash(ctx context.Context, gcsURI string) (string, error) {
	bucket, object, err := parseGCSURI(gcsURI)
	if err != nil {
		return "", err
	}
	obj := c.storageClient.Bucket(bucket).Object(object)
	attrs, err := obj.Attrs(ctx)
	if err != nil {
		return "", fmt.Errorf("GCSObjectHash attrs %s: %w", gcsURI, err)
	}
	// Prefer stored MD5 — no download needed.
	if len(attrs.MD5) > 0 {
		h := sha256.Sum256(attrs.MD5)
		return hex.EncodeToString(h[:]), nil
	}
	// Fallback: stream the object and hash its content.
	r, err := obj.NewReader(ctx)
	if err != nil {
		return "", fmt.Errorf("GCSObjectHash reader %s: %w", gcsURI, err)
	}
	defer r.Close()
	h := sha256.New()
	if _, err := io.Copy(h, r); err != nil {
		return "", fmt.Errorf("GCSObjectHash copy %s: %w", gcsURI, err)
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

// GCSObjectExists returns true if the object exists in GCS.
func (c *CloudRunManager) GCSObjectExists(ctx context.Context, gcsURI string) (bool, error) {
	bucket, object, err := parseGCSURI(gcsURI)
	if err != nil {
		return false, err
	}
	it := c.storageClient.Bucket(bucket).Objects(ctx, &storage.Query{Prefix: object})
	_, err = it.Next()
	if err == iterator.Done {
		return false, nil
	}
	if err != nil {
		return false, fmt.Errorf("GCSObjectExists %s: %w", gcsURI, err)
	}
	return true, nil
}

// WriteSentinel writes an empty object to gcsURI as a cache sentinel.
func (c *CloudRunManager) WriteSentinel(ctx context.Context, gcsURI string) error {
	bucket, object, err := parseGCSURI(gcsURI)
	if err != nil {
		return err
	}
	w := c.storageClient.Bucket(bucket).Object(object).NewWriter(ctx)
	if err := w.Close(); err != nil {
		return fmt.Errorf("WriteSentinel %s: %w", gcsURI, err)
	}
	return nil
}
