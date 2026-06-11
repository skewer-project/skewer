package coordinator

import (
	"context"
	"encoding/json"
	"net"
	"strings"
	"testing"

	executions "cloud.google.com/go/workflows/executions/apiv1"
	executionspb "cloud.google.com/go/workflows/executions/apiv1/executionspb"
	"github.com/fsouza/fake-gcs-server/fakestorage"
	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
	"google.golang.org/api/option"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/test/bufconn"
)

const coordinatorTestBucket = "test-bucket"

func TestExecutePipelineWorkflowArguments(t *testing.T) {
	t.Run("static scene with context and cache", func(t *testing.T) {
		sceneURI := "gs://test-bucket/uploads/p-static/scene.json"
		scene := sceneJSON{
			Layers:    []string{"layers/bg.json"},
			Context:   []string{"ctx/lights.json"},
			Animation: &animationJSON{Start: 0, End: 1, FPS: 24, ShutterAngle: 180},
			Camera:    &cameraJSON{},
		}
		objects := []fakestorage.Object{
			gcsObject("uploads/p-static/scene.json", mustJSON(t, scene)),
			gcsObject("uploads/p-static/layers/bg.json", []byte(`{"animated":false}`)),
			gcsObject("uploads/p-static/ctx/lights.json", []byte(`{"animated":false,"kind":"light"}`)),
		}

		got := executePipelineAndDecodeArgs(t, objects, &pb.SubmitPipelineRequest{
			PipelineId:               "p-static",
			SceneUri:                 sceneURI,
			CompositeOutputUriPrefix: "gs://test-bucket/composites/p-static/",
			EnableCache:              true,
		})

		sceneBase := "gs://test-bucket/uploads/p-static"
		layerURI := sceneBase + "/layers/bg.json"
		contextURIs := []string{sceneBase + "/ctx/lights.json"}
		cacheKey := mustCacheKey(t, objects, sceneURI, layerURI, contextURIs, "skewer:latest", "static")
		want := map[string]any{
			"pipeline_id":               "p-static",
			"project_id":                "project",
			"region":                    "us-central1",
			"num_frames":                float64(24),
			"frames_per_task":           float64(8),
			"data_bucket":               "data-bucket",
			"cache_bucket":              "cache-bucket",
			"skewer_image":              "skewer:latest",
			"loom_image":                "loom:latest",
			"stitch_image":              "stitch:latest",
			"skewer_machine_type":       "n2d-highcpu-8",
			"skewer_cpu_milli":          float64(8000),
			"skewer_memory_mib":         float64(6144),
			"skewer_provisioning_model": "SPOT",
			"skewer_max_retry_count":    float64(3),
			"loom_machine_type":         "n2d-standard-16",
			"loom_cpu_milli":            float64(16000),
			"loom_memory_mib":           float64(65536),
			"loom_provisioning_model":   "STANDARD",
			"loom_max_retry_count":      float64(2),
			"loom_frames_per_task":      float64(50),
			"loom_frame_parallelism":    float64(4),
			"loom_parallelism":          float64(16),
			"skewer_parallelism":        float64(24),
			"render_layer_parallelism":  float64(1),
			"network":                   "projects/project/global/networks/default",
			"subnet":                    "projects/project/regions/us-central1/subnetworks/default",
			"batch_sa":                  "batch@example.com",
			"batch_allowed_locations":   []any{"regions/us-central1"},
			"composite_output_prefix":   "gs://test-bucket/composites/p-static/",
			"stitch_fps":                float64(0),
			"layers": []any{
				map[string]any{
					"layer_id":     "bg",
					"layer_uri":    layerURI,
					"scene_uri":    sceneURI,
					"context_uris": []any{contextURIs[0]},
					"mode":         "static",
					"cache_key":    cacheKey,
					"enable_cache": true,
				},
			},
		}
		assertJSONEqual(t, want, got)
	})

	t.Run("animated layer and camera animation", func(t *testing.T) {
		sceneURI := "gs://test-bucket/uploads/p-animated/scene.json"
		scene := sceneJSON{
			Layers: []string{"layers/fg.json"},
			Camera: &cameraJSON{
				Keyframes: []map[string]any{{"time": 0.0}, {"time": 1.0}},
			},
			Animation: &animationJSON{Start: 0, End: 2.5, FPS: 24, ShutterAngle: 180},
		}
		objects := []fakestorage.Object{
			gcsObject("uploads/p-animated/scene.json", mustJSON(t, scene)),
			gcsObject("uploads/p-animated/layers/fg.json", []byte(`{"animated":true}`)),
		}

		got := executePipelineAndDecodeArgs(t, objects, &pb.SubmitPipelineRequest{
			PipelineId:               "p-animated",
			SceneUri:                 sceneURI,
			CompositeOutputUriPrefix: "gs://test-bucket/composites/p-animated/",
			EnableCache:              true,
			StitchFps:                30,
		})

		layer := got["layers"].([]any)[0].(map[string]any)
		if got["num_frames"] != float64(60) {
			t.Fatalf("num_frames = %v", got["num_frames"])
		}
		if got["stitch_fps"] != float64(30) {
			t.Fatalf("stitch_fps = %v", got["stitch_fps"])
		}
		if layer["mode"] != "animated" {
			t.Fatalf("mode = %v", layer["mode"])
		}
		if layer["enable_cache"] != true || layer["cache_key"] == "" {
			t.Fatalf("layer cache fields = %#v", layer)
		}
	})

	t.Run("cache failure disables cache for that layer", func(t *testing.T) {
		sceneURI := "gs://test-bucket/uploads/p-cache-fail/scene.json"
		scene := sceneJSON{
			Layers:    []string{"layers/bg.json"},
			Context:   []string{"ctx/missing.json"},
			Animation: &animationJSON{Start: 0, End: 1, FPS: 24, ShutterAngle: 180},
			Camera:    &cameraJSON{},
		}
		objects := []fakestorage.Object{
			gcsObject("uploads/p-cache-fail/scene.json", mustJSON(t, scene)),
			gcsObject("uploads/p-cache-fail/layers/bg.json", []byte(`{"animated":false}`)),
		}

		got := executePipelineAndDecodeArgs(t, objects, &pb.SubmitPipelineRequest{
			PipelineId:               "p-cache-fail",
			SceneUri:                 sceneURI,
			CompositeOutputUriPrefix: "gs://test-bucket/composites/p-cache-fail/",
			EnableCache:              true,
		})

		layer := got["layers"].([]any)[0].(map[string]any)
		if layer["enable_cache"] != false {
			t.Fatalf("enable_cache = %#v", layer["enable_cache"])
		}
		if layer["cache_key"] != "" {
			t.Fatalf("cache_key = %#v", layer["cache_key"])
		}
	})

	t.Run("overlong layer id is rejected", func(t *testing.T) {
		scene := sceneJSON{
			Layers:    []string{"layers/this-layer-name-is-too-long.json"},
			Animation: &animationJSON{Start: 0, End: 1, FPS: 24, ShutterAngle: 180},
			Camera:    &cameraJSON{},
		}
		objects := []fakestorage.Object{
			gcsObject("uploads/p-layer-limit/scene.json", mustJSON(t, scene)),
			gcsObject("uploads/p-layer-limit/layers/this-layer-name-is-too-long.json", []byte(`{"animated":false}`)),
		}

		manager, _, cleanup := newGCPManagerTestHarness(t, objects)
		defer cleanup()

		_, err := manager.ExecutePipeline(context.Background(), &pb.SubmitPipelineRequest{
			PipelineId:               "p-layer-limit",
			SceneUri:                 "gs://test-bucket/uploads/p-layer-limit/scene.json",
			CompositeOutputUriPrefix: "gs://test-bucket/composites/p-layer-limit/",
		})
		if err == nil || !strings.Contains(err.Error(), "exceeds 16-char limit") {
			t.Fatalf("err = %v", err)
		}
	})
}

type sceneJSON struct {
	Layers    []string       `json:"layers"`
	Context   []string       `json:"context,omitempty"`
	Camera    *cameraJSON    `json:"camera,omitempty"`
	Animation *animationJSON `json:"animation,omitempty"`
}

type cameraJSON struct {
	Keyframes []map[string]any `json:"keyframes,omitempty"`
}

type animationJSON struct {
	Start        float64 `json:"start"`
	End          float64 `json:"end"`
	FPS          float64 `json:"fps"`
	ShutterAngle float64 `json:"shutter_angle"`
}

func executePipelineAndDecodeArgs(t *testing.T, objects []fakestorage.Object, req *pb.SubmitPipelineRequest) map[string]any {
	t.Helper()
	manager, workflowStub, cleanup := newGCPManagerTestHarness(t, objects)
	defer cleanup()

	execName, err := manager.ExecutePipeline(context.Background(), req)
	if err != nil {
		t.Fatalf("ExecutePipeline() error = %v", err)
	}
	if execName != "executions/test" {
		t.Fatalf("execution name = %q", execName)
	}

	if workflowStub.lastCreate == nil {
		t.Fatal("CreateExecution not called")
	}
	var got map[string]any
	if err := json.Unmarshal([]byte(workflowStub.lastCreate.Execution.Argument), &got); err != nil {
		t.Fatalf("unmarshal workflow args: %v", err)
	}
	return got
}

func newGCPManagerTestHarness(t *testing.T, objects []fakestorage.Object) (*GCPManager, *recordingExecutionsServer, func()) {
	t.Helper()
	fakeStorage := fakestorage.NewServer(objects)
	storageClient := fakeStorage.Client()

	workflowStub := &recordingExecutionsServer{}
	listener := bufconn.Listen(1 << 20)
	grpcServer := grpc.NewServer()
	executionspb.RegisterExecutionsServer(grpcServer, workflowStub)
	go func() {
		_ = grpcServer.Serve(listener)
	}()

	conn, err := grpc.NewClient(
		"passthrough:///bufnet",
		grpc.WithContextDialer(func(context.Context, string) (net.Conn, error) {
			return listener.Dial()
		}),
		grpc.WithTransportCredentials(insecure.NewCredentials()),
	)
	if err != nil {
		t.Fatalf("grpc.NewClient() error = %v", err)
	}

	workflowsClient, err := executions.NewClient(context.Background(), option.WithGRPCConn(conn), option.WithoutAuthentication())
	if err != nil {
		t.Fatalf("executions.NewClient() error = %v", err)
	}

	manager := &GCPManager{
		projectID:               "project",
		region:                  "us-central1",
		workflowsClient:         workflowsClient,
		storageClient:           storageClient,
		workflowName:            "projects/project/locations/us-central1/workflows/render",
		dataBucket:              "data-bucket",
		cacheBucket:             "cache-bucket",
		skewerImage:             "skewer:latest",
		loomImage:               "loom:latest",
		stitchImage:             "stitch:latest",
		skewerMachineType:       "n2d-highcpu-8",
		skewerCPUMilli:          8000,
		skewerMemoryMiB:         6144,
		skewerProvisioningModel: "SPOT",
		skewerMaxRetryCount:     3,
		loomMachineType:         "n2d-standard-16",
		loomCPUMilli:            16000,
		loomMemoryMiB:           65536,
		loomProvisioningModel:   "STANDARD",
		loomMaxRetryCount:       2,
		loomFramesPerTask:       50,
		loomFrameParallelism:    4,
		loomParallelism:         16,
		skewerParallelism:       24,
		renderLayerParallelism:  1,
		network:                 "projects/project/global/networks/default",
		subnet:                  "projects/project/regions/us-central1/subnetworks/default",
		batchSA:                 "batch@example.com",
		batchAllowedLocations:   []string{"regions/us-central1"},
		framesPerTask:           8,
	}

	cleanup := func() {
		grpcServer.Stop()
		listener.Close()
		workflowsClient.Close()
		conn.Close()
		storageClient.Close()
		fakeStorage.Stop()
	}
	return manager, workflowStub, cleanup
}

type recordingExecutionsServer struct {
	executionspb.UnimplementedExecutionsServer
	lastCreate *executionspb.CreateExecutionRequest
}

func (f *recordingExecutionsServer) CreateExecution(_ context.Context, req *executionspb.CreateExecutionRequest) (*executionspb.Execution, error) {
	f.lastCreate = req
	return &executionspb.Execution{Name: "executions/test"}, nil
}

func (f *recordingExecutionsServer) GetExecution(context.Context, *executionspb.GetExecutionRequest) (*executionspb.Execution, error) {
	return &executionspb.Execution{}, nil
}

func (f *recordingExecutionsServer) CancelExecution(context.Context, *executionspb.CancelExecutionRequest) (*executionspb.Execution, error) {
	return &executionspb.Execution{}, nil
}

func gcsObject(name string, content []byte) fakestorage.Object {
	return fakestorage.Object{
		ObjectAttrs: fakestorage.ObjectAttrs{BucketName: coordinatorTestBucket, Name: name},
		Content:     content,
	}
}

func mustJSON(t *testing.T, v any) []byte {
	t.Helper()
	data, err := json.Marshal(v)
	if err != nil {
		t.Fatalf("json.Marshal() error = %v", err)
	}
	return data
}

func mustCacheKey(t *testing.T, objects []fakestorage.Object, sceneURI, layerURI string, contextURIs []string, rendererImage, mode string) string {
	t.Helper()
	fakeStorage := fakestorage.NewServer(objects)
	defer fakeStorage.Stop()
	client := fakeStorage.Client()
	defer client.Close()
	key, err := ComputeLayerCacheKey(context.Background(), client, sceneURI, layerURI, contextURIs, rendererImage, mode)
	if err != nil {
		t.Fatalf("ComputeLayerCacheKey() error = %v", err)
	}
	return key
}

func assertJSONEqual(t *testing.T, want, got any) {
	t.Helper()
	wantJSON := mustJSON(t, want)
	gotJSON := mustJSON(t, got)
	if string(wantJSON) != string(gotJSON) {
		t.Fatalf("json mismatch\nwant: %s\ngot:  %s", wantJSON, gotJSON)
	}
}
