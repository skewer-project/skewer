package api

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"strings"

	"cloud.google.com/go/storage"
)

// OwnerStore reads and writes the per-pipeline ownership marker and the
// workflow-execution-name marker inside the upload prefix.
type OwnerStore struct {
	client *storage.Client
	bucket string
}

type executionMarker struct {
	ExecutionName            string `json:"execution_name"`
	CompositeOutputURIPrefix string `json:"composite_output_uri_prefix,omitempty"`
}

func NewOwnerStore(client *storage.Client, dataBucket string) *OwnerStore {
	return &OwnerStore{client: client, bucket: dataBucket}
}

func ownerObjectKey(pipelineID string) string {
	return fmt.Sprintf("uploads/%s/_owner.txt", pipelineID)
}

func executionObjectKey(pipelineID string) string {
	return fmt.Sprintf("uploads/%s/_execution.txt", pipelineID)
}

// Write stores the email as the owner of the pipeline. Returns an error if
// the marker already exists (prevents replay / collision).
func (o *OwnerStore) Write(ctx context.Context, pipelineID, email string) error {
	obj := o.client.Bucket(o.bucket).Object(ownerObjectKey(pipelineID))
	// DoesNotExist precondition: error if another caller already claimed this ID.
	w := obj.If(storage.Conditions{DoesNotExist: true}).NewWriter(ctx)
	w.ContentType = "text/plain"
	if _, err := io.WriteString(w, strings.ToLower(email)); err != nil {
		_ = w.Close()
		return fmt.Errorf("write owner marker: %w", err)
	}
	if err := w.Close(); err != nil {
		return fmt.Errorf("close owner marker: %w", err)
	}
	return nil
}

// Require asserts that the caller's email matches the owner marker. Returns
// a sentinel error the handler can translate to 403 / 404.
func (o *OwnerStore) Require(ctx context.Context, pipelineID, email string) error {
	obj := o.client.Bucket(o.bucket).Object(ownerObjectKey(pipelineID))
	r, err := obj.NewReader(ctx)
	if err != nil {
		if errors.Is(err, storage.ErrObjectNotExist) {
			return ErrPipelineNotFound
		}
		return fmt.Errorf("read owner marker: %w", err)
	}
	defer r.Close()
	buf, err := io.ReadAll(r)
	if err != nil {
		return fmt.Errorf("read owner marker body: %w", err)
	}
	owner := strings.TrimSpace(strings.ToLower(string(buf)))
	if owner != strings.ToLower(email) {
		return ErrPipelineForbidden
	}
	return nil
}

// WriteExecution records the Cloud Workflows execution name and output prefix
// so status/cancel/artifact handlers can look them up later.
func (o *OwnerStore) WriteExecution(ctx context.Context, pipelineID, executionName, compositeOutputURIPrefix string) error {
	obj := o.client.Bucket(o.bucket).Object(executionObjectKey(pipelineID))
	w := obj.NewWriter(ctx)
	w.ContentType = "application/json"
	body, err := json.Marshal(executionMarker{
		ExecutionName:            executionName,
		CompositeOutputURIPrefix: compositeOutputURIPrefix,
	})
	if err != nil {
		return fmt.Errorf("marshal execution marker: %w", err)
	}
	if _, err := w.Write(body); err != nil {
		_ = w.Close()
		return fmt.Errorf("write execution marker: %w", err)
	}
	if err := w.Close(); err != nil {
		return fmt.Errorf("close execution marker: %w", err)
	}
	return nil
}

func (o *OwnerStore) readExecutionMarker(ctx context.Context, pipelineID string) (executionMarker, error) {
	obj := o.client.Bucket(o.bucket).Object(executionObjectKey(pipelineID))
	r, err := obj.NewReader(ctx)
	if err != nil {
		if errors.Is(err, storage.ErrObjectNotExist) {
			return executionMarker{}, ErrPipelineNotFound
		}
		return executionMarker{}, fmt.Errorf("read execution marker: %w", err)
	}
	defer r.Close()
	buf, err := io.ReadAll(r)
	if err != nil {
		return executionMarker{}, fmt.Errorf("read execution marker body: %w", err)
	}
	raw := strings.TrimSpace(string(buf))
	if raw == "" {
		return executionMarker{}, ErrPipelineNotFound
	}
	if raw[0] != '{' {
		return executionMarker{ExecutionName: raw}, nil
	}
	var marker executionMarker
	if err := json.Unmarshal([]byte(raw), &marker); err != nil {
		return executionMarker{}, fmt.Errorf("decode execution marker: %w", err)
	}
	if marker.ExecutionName == "" {
		return executionMarker{}, ErrPipelineNotFound
	}
	return marker, nil
}

// ReadExecution returns the Cloud Workflows execution name for the pipeline,
// or ErrPipelineNotFound if the marker is missing.
func (o *OwnerStore) ReadExecution(ctx context.Context, pipelineID string) (string, error) {
	marker, err := o.readExecutionMarker(ctx, pipelineID)
	if err != nil {
		return "", err
	}
	return marker.ExecutionName, nil
}

func (o *OwnerStore) ReadCompositeOutputPrefix(ctx context.Context, pipelineID string) (string, error) {
	marker, err := o.readExecutionMarker(ctx, pipelineID)
	if err != nil {
		return "", err
	}
	if marker.CompositeOutputURIPrefix == "" {
		return "", ErrPipelineNotFound
	}
	return marker.CompositeOutputURIPrefix, nil
}

var (
	ErrPipelineNotFound  = errors.New("pipeline not found")
	ErrPipelineForbidden = errors.New("pipeline access forbidden")
)
