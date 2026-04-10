package coordinator

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"strings"

	executions "cloud.google.com/go/workflows/executions/apiv1"
	executionspb "cloud.google.com/go/workflows/executions/apiv1/executionspb"
	"cloud.google.com/go/storage"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

// GCPManager delegates pipeline orchestration to Cloud Workflows and Cloud Storage.
// It is stateless: all execution state lives in Cloud Workflows.
type GCPManager struct {
	projectID       string // used when submitting Batch jobs via the workflow
	region          string // used when submitting Batch jobs via the workflow
	workflowsClient *executions.Client
	storageClient   *storage.Client
	workflowName    string // fully-qualified: projects/{p}/locations/{r}/workflows/{w}
	dataBucket      string
	cacheBucket     string
	skewerImage     string
	loomImage       string
	machineType     string
	network         string
	subnet          string
	batchSA         string
}

// NewGCPManager reads config from environment variables and creates GCP clients.
// All env vars are set by Terraform on the Cloud Run service.
func NewGCPManager(ctx context.Context) (*GCPManager, error) {
	// WORKFLOW_NAME is set by Terraform as the full resource ID:
	// projects/{project}/locations/{region}/workflows/{name}
	workflowName := mustEnv("WORKFLOW_NAME")

	wfClient, err := executions.NewClient(ctx)
	if err != nil {
		return nil, fmt.Errorf("create workflows executions client: %w", err)
	}

	storageClient, err := storage.NewClient(ctx)
	if err != nil {
		return nil, fmt.Errorf("create storage client: %w", err)
	}

	return &GCPManager{
		projectID:       mustEnv("GCP_PROJECT"),  // passed into workflow args for Batch job creation
		region:          mustEnv("GCP_REGION"),    // passed into workflow args for Batch job creation
		workflowsClient: wfClient,
		storageClient:   storageClient,
		workflowName:    workflowName,
		dataBucket:      mustEnv("DATA_BUCKET"),
		cacheBucket:     mustEnv("CACHE_BUCKET"),
		skewerImage:     mustEnv("SKEWER_IMAGE"),
		loomImage:       mustEnv("LOOM_IMAGE"),
		machineType:     getEnvOrDefault("BATCH_MACHINE_TYPE", "e2-standard-4"),
		network:         mustEnv("VPC_NETWORK"),
		subnet:          mustEnv("VPC_SUBNET"),
		batchSA:         mustEnv("BATCH_SA_EMAIL"),
	}, nil
}

// ExecutePipeline computes cache keys, builds workflow arguments, and creates a
// Cloud Workflows execution. Returns the execution name used as the pipeline ID.
func (m *GCPManager) ExecutePipeline(ctx context.Context, req *pb.SubmitPipelineRequest) (string, error) {
	type layerArg struct {
		LayerID    string `json:"layer_id"`
		SceneURI   string `json:"scene_uri"`
		CacheKey   string `json:"cache_key"`
		EnableCache bool  `json:"enable_cache"`
	}

	layers := make([]layerArg, 0, len(req.Layers))
	for _, l := range req.Layers {
		cacheKey := ""
		enableCache := l.EnableCache
		if l.EnableCache {
			var err error
			cacheKey, err = ComputeLayerCacheKey(ctx, m.storageClient, l.SceneUri)
			if err != nil {
				log.Printf("[GCP]: cache key computation failed for %s: %v (disabling cache for this layer)", l.LayerId, err)
				enableCache = false
			}
		}
		layers = append(layers, layerArg{
			LayerID:     l.LayerId,
			SceneURI:    l.SceneUri,
			CacheKey:    cacheKey,
			EnableCache: enableCache,
		})
	}

	// Build camera args (defaults if not provided)
	camArgs := map[string]float64{
		"from_x": 0, "from_y": 0, "from_z": 5,
		"at_x": 0, "at_y": 0, "at_z": 0,
		"vup_x": 0, "vup_y": 1, "vup_z": 0,
		"vfov": 90,
	}
	if c := req.Camera; c != nil {
		if len(c.LookFrom) == 3 {
			camArgs["from_x"] = float64(c.LookFrom[0])
			camArgs["from_y"] = float64(c.LookFrom[1])
			camArgs["from_z"] = float64(c.LookFrom[2])
		}
		if len(c.LookAt) == 3 {
			camArgs["at_x"] = float64(c.LookAt[0])
			camArgs["at_y"] = float64(c.LookAt[1])
			camArgs["at_z"] = float64(c.LookAt[2])
		}
		if len(c.Vup) == 3 {
			camArgs["vup_x"] = float64(c.Vup[0])
			camArgs["vup_y"] = float64(c.Vup[1])
			camArgs["vup_z"] = float64(c.Vup[2])
		}
		if c.Vfov > 0 {
			camArgs["vfov"] = float64(c.Vfov)
		}
	}

	// Convert context URIs for the workflow (may be empty)
	contextURIs := req.ContextUris
	if contextURIs == nil {
		contextURIs = []string{}
	}

	args := map[string]any{
		"pipeline_id":              req.PipelineId,
		"project_id":               m.projectID,
		"region":                   m.region,
		"num_frames":               req.NumFrames,
		"layers":                   layers,
		"data_bucket":              m.dataBucket,
		"cache_bucket":             m.cacheBucket,
		"skewer_image":             m.skewerImage,
		"loom_image":               m.loomImage,
		"machine_type":             m.machineType,
		"network":                  m.network,
		"subnet":                   m.subnet,
		"batch_sa":                 m.batchSA,
		"composite_output_prefix":  req.CompositeOutputUriPrefix,
		"camera":                   camArgs,
		"context_uris":             contextURIs,
	}

	argsJSON, err := json.Marshal(args)
	if err != nil {
		return "", fmt.Errorf("marshal workflow args: %w", err)
	}

	exec, err := m.workflowsClient.CreateExecution(ctx, &executionspb.CreateExecutionRequest{
		Parent: m.workflowName,
		Execution: &executionspb.Execution{
			Argument: string(argsJSON),
		},
	})
	if err != nil {
		return "", fmt.Errorf("create workflow execution: %w", err)
	}

	// The execution name is the full resource path; use it as the pipeline ID
	return exec.Name, nil
}

// GetPipelineStatus retrieves a Cloud Workflows execution and maps it to the proto status.
func (m *GCPManager) GetPipelineStatus(ctx context.Context, pipelineID string) (*pb.GetPipelineStatusResponse, error) {
	exec, err := m.workflowsClient.GetExecution(ctx, &executionspb.GetExecutionRequest{
		Name: pipelineID,
	})
	if err != nil {
		return nil, fmt.Errorf("get workflow execution: %w", err)
	}

	resp := &pb.GetPipelineStatusResponse{
		Status: mapWorkflowState(exec.State),
	}

	if exec.Error != nil {
		resp.ErrorMessage = exec.Error.Payload
	}

	// Parse result JSON to extract layer_outputs and composite_output
	if exec.Result != "" {
		var result struct {
			LayerOutputs   map[string]string `json:"layer_outputs"`
			OutputURI      string            `json:"output_uri"`
		}
		if err := json.Unmarshal([]byte(exec.Result), &result); err == nil {
			resp.LayerOutputs = result.LayerOutputs
			resp.CompositeOutput = result.OutputURI
		}
	}

	return resp, nil
}

// CancelPipeline cancels an in-progress Cloud Workflows execution.
func (m *GCPManager) CancelPipeline(ctx context.Context, pipelineID string) error {
	_, err := m.workflowsClient.CancelExecution(ctx, &executionspb.CancelExecutionRequest{
		Name: pipelineID,
	})
	if err != nil {
		return fmt.Errorf("cancel workflow execution: %w", err)
	}
	return nil
}

func mapWorkflowState(state executionspb.Execution_State) pb.GetPipelineStatusResponse_PipelineStatus {
	switch state {
	case executionspb.Execution_ACTIVE:
		return pb.GetPipelineStatusResponse_PIPELINE_STATUS_RUNNING
	case executionspb.Execution_SUCCEEDED:
		return pb.GetPipelineStatusResponse_PIPELINE_STATUS_SUCCEEDED
	case executionspb.Execution_FAILED:
		return pb.GetPipelineStatusResponse_PIPELINE_STATUS_FAILED
	case executionspb.Execution_CANCELLED:
		return pb.GetPipelineStatusResponse_PIPELINE_STATUS_CANCELLED
	default:
		return pb.GetPipelineStatusResponse_PIPELINE_STATUS_UNSPECIFIED
	}
}

func mustEnv(key string) string {
	v := os.Getenv(key)
	if v == "" {
		log.Fatalf("[GCP]: required env var %s is not set", key)
	}
	return v
}

func getEnvOrDefault(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

// sceneURIToBucketObject parses "gs://bucket/path/to/object" into (bucket, object).
func sceneURIToBucketObject(uri string) (string, string, error) {
	if !strings.HasPrefix(uri, "gs://") {
		return "", "", fmt.Errorf("scene URI must start with gs://: %q", uri)
	}
	trimmed := strings.TrimPrefix(uri, "gs://")
	idx := strings.IndexByte(trimmed, '/')
	if idx < 0 {
		return "", "", fmt.Errorf("scene URI has no object path: %q", uri)
	}
	return trimmed[:idx], trimmed[idx+1:], nil
}
