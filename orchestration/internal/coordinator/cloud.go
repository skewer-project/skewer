package coordinator

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"math"
	"os"
	"path"
	"strconv"
	"strings"

	"cloud.google.com/go/storage"
	executions "cloud.google.com/go/workflows/executions/apiv1"
	executionspb "cloud.google.com/go/workflows/executions/apiv1/executionspb"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

// GCPManager delegates pipeline orchestration to Cloud Workflows and Cloud Storage.
// It is stateless: all execution state lives in Cloud Workflows.
type GCPManager struct {
	projectID               string
	region                  string
	workflowsClient         *executions.Client
	storageClient           *storage.Client
	workflowName            string
	dataBucket              string
	cacheBucket             string
	skewerImage             string
	loomImage               string
	skewerMachineType       string
	skewerCPUMilli          int
	skewerMemoryMiB         int
	skewerProvisioningModel string
	skewerMaxRetryCount     int
	loomMachineType         string
	loomCPUMilli            int
	loomMemoryMiB           int
	loomProvisioningModel   string
	loomMaxRetryCount       int
	loomParallelism         int
	network                 string
	subnet                  string
	batchSA                 string
	framesPerTask           int
}

// NewGCPManager reads config from environment variables and creates GCP clients.
func NewGCPManager(ctx context.Context) (*GCPManager, error) {
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
		projectID:               mustEnv("GCP_PROJECT"),
		region:                  mustEnv("GCP_REGION"),
		workflowsClient:         wfClient,
		storageClient:           storageClient,
		workflowName:            workflowName,
		dataBucket:              mustEnv("DATA_BUCKET"),
		cacheBucket:             mustEnv("CACHE_BUCKET"),
		skewerImage:             mustEnv("SKEWER_IMAGE"),
		loomImage:               mustEnv("LOOM_IMAGE"),
		skewerMachineType:       getEnvOrDefault("SKEWER_BATCH_MACHINE_TYPE", "n2d-highcpu-8"),
		skewerCPUMilli:          getEnvIntOrDefault("SKEWER_BATCH_CPU_MILLI", 8000),
		skewerMemoryMiB:         getEnvIntOrDefault("SKEWER_BATCH_MEMORY_MIB", 6144),
		skewerProvisioningModel: getEnvOrDefault("SKEWER_BATCH_PROVISIONING_MODEL", "SPOT"),
		skewerMaxRetryCount:     getEnvIntOrDefault("SKEWER_BATCH_MAX_RETRY_COUNT", 3),
		loomMachineType:         getEnvOrDefault("LOOM_BATCH_MACHINE_TYPE", "e2-highmem-8"),
		loomCPUMilli:            getEnvIntOrDefault("LOOM_BATCH_CPU_MILLI", 8000),
		loomMemoryMiB:           getEnvIntOrDefault("LOOM_BATCH_MEMORY_MIB", 32768),
		loomProvisioningModel:   getEnvOrDefault("LOOM_BATCH_PROVISIONING_MODEL", "STANDARD"),
		loomMaxRetryCount:       getEnvIntOrDefault("LOOM_BATCH_MAX_RETRY_COUNT", 2),
		loomParallelism:         getEnvIntOrDefault("LOOM_BATCH_PARALLELISM", 16),
		network:                 mustEnv("VPC_NETWORK"),
		subnet:                  mustEnv("VPC_SUBNET"),
		batchSA:                 mustEnv("BATCH_SA_EMAIL"),
		framesPerTask:           getEnvIntOrDefault("SKEWER_BATCH_FRAMES_PER_TASK", 8),
	}, nil
}

// layerDescriptor is the internal representation of one render layer
// derived from parsing the root scene.json.
type layerDescriptor struct {
	LayerID     string   `json:"layer_id"`
	LayerURI    string   `json:"layer_uri"`    // GCS URI of the layer JSON file
	SceneURI    string   `json:"scene_uri"`    // GCS URI of the root scene.json
	ContextURIs []string `json:"context_uris"` // GCS URIs of context files
	Mode        string   `json:"mode"`         // "static" or "animated"
	CacheKey    string   `json:"cache_key"`
	EnableCache bool     `json:"enable_cache"`
}

// minimalSceneJSON is the minimal subset of scene.json parsed by the coordinator.
// It mirrors the keys in the C++ SceneConfig / LoadSceneFile.
type minimalSceneJSON struct {
	Layers  []string `json:"layers"`
	Context []string `json:"context"`
	Animation *struct {
		Start        float64 `json:"start"`
		End          float64 `json:"end"`
		FPS          float64 `json:"fps"`
		ShutterAngle float64 `json:"shutter_angle"`
	} `json:"animation"`
}

// minimalLayerJSON is the subset of a layer JSON needed for animation classification.
type minimalLayerJSON struct {
	Animated *bool `json:"animated"`
}

// ExecutePipeline parses the root scene, derives layer descriptors, builds
// workflow arguments, and creates a Cloud Workflows execution.
func (m *GCPManager) ExecutePipeline(ctx context.Context, req *pb.SubmitPipelineRequest) (string, error) {
	sceneBytes, err := downloadGCSBytes(ctx, m.storageClient, req.SceneUri)
	if err != nil {
		return "", fmt.Errorf("download scene %s: %w", req.SceneUri, err)
	}

	var sceneJSON minimalSceneJSON
	if err := json.Unmarshal(sceneBytes, &sceneJSON); err != nil {
		return "", fmt.Errorf("parse scene JSON: %w", err)
	}

	if len(sceneJSON.Layers) == 0 {
		return "", fmt.Errorf("scene %s has no layers", req.SceneUri)
	}

	// Derive num_frames from the animation block (default 1 for non-animated scenes).
	numFrames := 1
	if a := sceneJSON.Animation; a != nil && a.FPS > 0 {
		dur := a.End - a.Start
		if dur > 0 {
			numFrames = int(math.Round(dur * a.FPS))
		}
	}

	// Resolve layer URIs relative to the scene URI.
	sceneBase := gcsURIDir(req.SceneUri)

	contextURIs := make([]string, 0, len(sceneJSON.Context))
	for _, c := range sceneJSON.Context {
		contextURIs = append(contextURIs, resolveGCSURI(sceneBase, c))
	}

	// Build layer descriptors: peek each layer's animated flag.
	descriptors := make([]layerDescriptor, 0, len(sceneJSON.Layers))
	for i, layerRef := range sceneJSON.Layers {
		layerURI := resolveGCSURI(sceneBase, layerRef)

		animated, err := peekLayerAnimated(ctx, m.storageClient, layerURI)
		if err != nil {
			return "", fmt.Errorf("peek layer %s: %w", layerURI, err)
		}

		mode := "static"
		if animated {
			mode = "animated"
		}

		// Layer ID is the stem of the layer filename (matches C++ layer_stem convention).
		layerID := gcsFileStem(layerURI)
		if layerID == "" {
			layerID = fmt.Sprintf("layer%d", i)
		}

		cacheKey := ""
		enableCache := req.EnableCache
		if req.EnableCache {
			key, err := ComputeLayerCacheKey(ctx, m.storageClient, req.SceneUri, layerURI, contextURIs, m.skewerImage, mode)
			if err != nil {
				log.Printf("[GCP]: cache key computation failed for %s: %v (disabling cache for this layer)", layerID, err)
				enableCache = false
			} else {
				cacheKey = key
			}
		}

		descriptors = append(descriptors, layerDescriptor{
			LayerID:     layerID,
			LayerURI:    layerURI,
			SceneURI:    req.SceneUri,
			ContextURIs: contextURIs,
			Mode:        mode,
			CacheKey:    cacheKey,
			EnableCache: enableCache,
		})
	}

	args := map[string]any{
		"pipeline_id":               req.PipelineId,
		"project_id":                m.projectID,
		"region":                    m.region,
		"num_frames":                numFrames,
		"frames_per_task":           m.framesPerTask,
		"layers":                    descriptors,
		"data_bucket":               m.dataBucket,
		"cache_bucket":              m.cacheBucket,
		"skewer_image":              m.skewerImage,
		"loom_image":                m.loomImage,
		"skewer_machine_type":       m.skewerMachineType,
		"skewer_cpu_milli":          m.skewerCPUMilli,
		"skewer_memory_mib":         m.skewerMemoryMiB,
		"skewer_provisioning_model": m.skewerProvisioningModel,
		"skewer_max_retry_count":    m.skewerMaxRetryCount,
		"loom_machine_type":         m.loomMachineType,
		"loom_cpu_milli":            m.loomCPUMilli,
		"loom_memory_mib":           m.loomMemoryMiB,
		"loom_provisioning_model":   m.loomProvisioningModel,
		"loom_max_retry_count":      m.loomMaxRetryCount,
		"loom_parallelism":          m.loomParallelism,
		"network":                   m.network,
		"subnet":                    m.subnet,
		"batch_sa":                  m.batchSA,
		"composite_output_prefix":   req.CompositeOutputUriPrefix,
		"smear_fps":                 req.GetSmearFps(),
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

	if exec.Result != "" {
		var result struct {
			LayerOutputs map[string]string `json:"layer_outputs"`
			OutputURI    string            `json:"output_uri"`
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

// downloadGCSBytes downloads an object from GCS and returns its contents.
func downloadGCSBytes(ctx context.Context, client *storage.Client, uri string) ([]byte, error) {
	bucket, object, err := sceneURIToBucketObject(uri)
	if err != nil {
		return nil, err
	}
	r, err := client.Bucket(bucket).Object(object).NewReader(ctx)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", uri, err)
	}
	defer r.Close()
	return io.ReadAll(r)
}

// peekLayerAnimated downloads a layer JSON and returns its animated flag.
// Returns false when the key is absent (matches C++ PeekLayerAnimationFlags).
func peekLayerAnimated(ctx context.Context, client *storage.Client, layerURI string) (bool, error) {
	data, err := downloadGCSBytes(ctx, client, layerURI)
	if err != nil {
		return false, err
	}
	var lj minimalLayerJSON
	if err := json.Unmarshal(data, &lj); err != nil {
		return false, fmt.Errorf("parse layer JSON: %w", err)
	}
	if lj.Animated == nil {
		return false, nil
	}
	return *lj.Animated, nil
}

// gcsURIDir returns the "directory" portion of a GCS URI (everything up to and
// including the last slash), used to resolve relative layer/context paths.
func gcsURIDir(uri string) string {
	idx := strings.LastIndex(uri, "/")
	if idx < 0 {
		return uri
	}
	return uri[:idx+1]
}

// resolveGCSURI resolves a layer reference relative to a GCS directory prefix.
// Absolute gs:// URIs pass through unchanged.
func resolveGCSURI(baseDir, ref string) string {
	if strings.HasPrefix(ref, "gs://") {
		return ref
	}
	// Use path.Join for the object portion only (not the gs:// scheme).
	if strings.HasPrefix(baseDir, "gs://") {
		scheme := "gs://"
		rest := strings.TrimPrefix(baseDir, scheme)
		joined := path.Join(rest, ref)
		return scheme + joined
	}
	return path.Join(baseDir, ref)
}

// gcsFileStem returns the filename stem (no directory, no extension) of a GCS URI.
func gcsFileStem(uri string) string {
	base := path.Base(uri)
	ext := path.Ext(base)
	return strings.TrimSuffix(base, ext)
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

func getEnvIntOrDefault(key string, def int) int {
	if v := os.Getenv(key); v != "" {
		n, err := strconv.Atoi(v)
		if err != nil {
			log.Fatalf("[GCP]: env var %s must be an integer: %v", key, err)
		}
		return n
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
