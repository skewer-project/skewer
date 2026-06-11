package api

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"cloud.google.com/go/storage"
	"github.com/fsouza/fake-gcs-server/fakestorage"
	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

const (
	testBucket = "test-bucket"
	testEmail  = "owner@example.com"
)

type stubSigner struct{}

func (stubSigner) SignPut(bucket, object, contentType string, _ time.Duration) (string, error) {
	return fmt.Sprintf("https://signed.example/put/%s/%s?ct=%s", bucket, object, contentType), nil
}

func (stubSigner) SignGet(bucket, object string, _ time.Duration) (string, error) {
	return fmt.Sprintf("https://signed.example/get/%s/%s", bucket, object), nil
}

type stubCoordinator struct {
	submitFn func(context.Context, *pb.SubmitPipelineRequest) (*pb.SubmitPipelineResponse, error)
	statusFn func(context.Context, string) (*pb.GetPipelineStatusResponse, error)
	cancelFn func(context.Context, string) (*pb.CancelPipelineResponse, error)
}

func (s stubCoordinator) Submit(ctx context.Context, req *pb.SubmitPipelineRequest) (*pb.SubmitPipelineResponse, error) {
	return s.submitFn(ctx, req)
}

func (s stubCoordinator) Status(ctx context.Context, pipelineID string) (*pb.GetPipelineStatusResponse, error) {
	return s.statusFn(ctx, pipelineID)
}

func (s stubCoordinator) Cancel(ctx context.Context, pipelineID string) (*pb.CancelPipelineResponse, error) {
	return s.cancelFn(ctx, pipelineID)
}

func TestHandleInitRequiresAuthAndValidManifest(t *testing.T) {
	t.Run("unauthenticated", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{})
		res := h.do(t, "", http.MethodPost, "/v1/jobs/init", `{"files":[{"path":"scene.json"}]}`)
		assertStatus(t, res, http.StatusUnauthorized)
		assertErrorContains(t, res, "unauthenticated")
	})

	t.Run("malformed json", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{})
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/init", `{"files":`)
		assertStatus(t, res, http.StatusBadRequest)
		assertErrorContains(t, res, "invalid JSON body")
	})

	t.Run("path traversal rejected", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{})
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/init", `{"files":[{"path":"../scene.json"}]}`)
		assertStatus(t, res, http.StatusBadRequest)
		assertErrorContains(t, res, "invalid path segment")
	})

	t.Run("missing scene manifest rejected", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{})
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/init", `{"files":[{"path":"layers/layer.json"}]}`)
		assertStatus(t, res, http.StatusBadRequest)
		assertErrorContains(t, res, "manifest must include a scene.json")
	})

	t.Run("success signs cleaned paths", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{})
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/init", `{"files":[{"path":"scene.json","content_type":"application/json"},{"path":"layers/bg.json","content_type":"application/json"}]}`)
		assertStatus(t, res, http.StatusOK)

		var body initResponse
		decodeJSON(t, res, &body)
		if body.PipelineID == "" || !strings.HasPrefix(body.PipelineID, "p-") {
			t.Fatalf("unexpected pipeline id: %#v", body.PipelineID)
		}
		if got := body.UploadURLs["scene.json"]; !strings.Contains(got, "/uploads/"+body.PipelineID+"/scene.json") {
			t.Fatalf("scene upload url = %q", got)
		}
		if got := body.UploadURLs["layers/bg.json"]; !strings.Contains(got, "/uploads/"+body.PipelineID+"/layers/bg.json") {
			t.Fatalf("layer upload url = %q", got)
		}
		if _, err := h.storage.Bucket(testBucket).Object(ownerObjectKey(body.PipelineID)).Attrs(context.Background()); err != nil {
			t.Fatalf("owner marker missing: %v", err)
		}
	})
}

func TestHandleSubmitCoversOwnershipValidationAndCoordinatorFailures(t *testing.T) {
	scene := validSceneBundle("uploads/p-1234")

	t.Run("forbidden owner", func(t *testing.T) {
		h := newAPITestHarness(t, scene, stubCoordinator{})
		h.claimPipeline(t, "p-1234", testEmail)
		res := h.do(t, "other@example.com", http.MethodPost, "/v1/jobs/p-1234/submit", `{}`)
		assertStatus(t, res, http.StatusForbidden)
		assertErrorContains(t, res, "not authorized")
	})

	t.Run("malformed json", func(t *testing.T) {
		h := newAPITestHarness(t, scene, stubCoordinator{})
		h.claimPipeline(t, "p-1234", testEmail)
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/p-1234/submit", `{"scene_path":`)
		assertStatus(t, res, http.StatusBadRequest)
		assertErrorContains(t, res, "invalid JSON body")
	})

	t.Run("scene path traversal rejected", func(t *testing.T) {
		h := newAPITestHarness(t, scene, stubCoordinator{})
		h.claimPipeline(t, "p-1234", testEmail)
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/p-1234/submit", `{"scene_path":"../scene.json"}`)
		assertStatus(t, res, http.StatusBadRequest)
		assertErrorContains(t, res, "scene_path")
	})

	t.Run("bad composite prefix", func(t *testing.T) {
		h := newAPITestHarness(t, scene, stubCoordinator{})
		h.claimPipeline(t, "p-1234", testEmail)
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/p-1234/submit", `{"composite_output_uri_prefix":"https://example.com/not-gcs"}`)
		assertStatus(t, res, http.StatusBadRequest)
		assertErrorContains(t, res, "must start with gs://")
	})

	t.Run("missing pipeline id in composite prefix", func(t *testing.T) {
		h := newAPITestHarness(t, scene, stubCoordinator{})
		h.claimPipeline(t, "p-1234", testEmail)
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/p-1234/submit", `{"composite_output_uri_prefix":"gs://test-bucket/composites/other/"}`)
		assertStatus(t, res, http.StatusBadRequest)
		assertErrorContains(t, res, "must include the pipeline id")
	})

	t.Run("coordinator failure becomes bad gateway", func(t *testing.T) {
		h := newAPITestHarness(t, scene, stubCoordinator{
			submitFn: func(context.Context, *pb.SubmitPipelineRequest) (*pb.SubmitPipelineResponse, error) {
				return nil, errors.New("workflow unavailable")
			},
		})
		h.claimPipeline(t, "p-1234", testEmail)
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/p-1234/submit", `{}`)
		assertStatus(t, res, http.StatusBadGateway)
		assertErrorContains(t, res, "workflow unavailable")
	})

	t.Run("success records execution and default composite prefix", func(t *testing.T) {
		var gotReq *pb.SubmitPipelineRequest
		h := newAPITestHarness(t, scene, stubCoordinator{
			submitFn: func(_ context.Context, req *pb.SubmitPipelineRequest) (*pb.SubmitPipelineResponse, error) {
				gotReq = req
				return &pb.SubmitPipelineResponse{PipelineId: "executions/p-1234"}, nil
			},
		})
		h.claimPipeline(t, "p-1234", testEmail)
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/p-1234/submit", `{"enable_cache":true,"stitch_fps":24}`)
		assertStatus(t, res, http.StatusOK)

		if gotReq == nil {
			t.Fatal("coordinator submit not called")
		}
		if gotReq.SceneUri != "gs://test-bucket/uploads/p-1234/scene.json" {
			t.Fatalf("scene uri = %q", gotReq.SceneUri)
		}
		if gotReq.CompositeOutputUriPrefix != "gs://test-bucket/composites/p-1234/" {
			t.Fatalf("composite prefix = %q", gotReq.CompositeOutputUriPrefix)
		}
		if !gotReq.EnableCache || gotReq.StitchFps != 24 {
			t.Fatalf("submit request = %#v", gotReq)
		}
		execName, err := h.owner.ReadExecution(context.Background(), "p-1234")
		if err != nil || execName != "executions/p-1234" {
			t.Fatalf("execution marker = %q, %v", execName, err)
		}
	})
}

func TestHandleStatusAndCancelCoverPendingForbiddenAndCoordinatorFailures(t *testing.T) {
	t.Run("status pending returns conflict", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{})
		h.claimPipeline(t, "p-2000", testEmail)
		res := h.do(t, testEmail, http.MethodGet, "/v1/jobs/p-2000", "")
		assertStatus(t, res, http.StatusConflict)
		assertErrorContains(t, res, "has not been submitted yet")
	})

	t.Run("status coordinator failure", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{
			statusFn: func(context.Context, string) (*pb.GetPipelineStatusResponse, error) {
				return nil, errors.New("coordinator down")
			},
		})
		h.claimPipeline(t, "p-2001", testEmail)
		h.writeExecution(t, "p-2001", "executions/p-2001", "")
		res := h.do(t, testEmail, http.MethodGet, "/v1/jobs/p-2001", "")
		assertStatus(t, res, http.StatusBadGateway)
		assertErrorContains(t, res, "coordinator down")
	})

	t.Run("status success", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{
			statusFn: func(context.Context, string) (*pb.GetPipelineStatusResponse, error) {
				return &pb.GetPipelineStatusResponse{
					Status:          pb.GetPipelineStatusResponse_PIPELINE_STATUS_RUNNING,
					LayerOutputs:    map[string]string{"bg": "gs://bucket/renders/p-2002/bg.exr"},
					CompositeOutput: "gs://bucket/composites/p-2002/output.mp4",
				}, nil
			},
		})
		h.claimPipeline(t, "p-2002", testEmail)
		h.writeExecution(t, "p-2002", "executions/p-2002", "")
		res := h.do(t, testEmail, http.MethodGet, "/v1/jobs/p-2002", "")
		assertStatus(t, res, http.StatusOK)
		var body statusResponse
		decodeJSON(t, res, &body)
		if body.Status != "PIPELINE_STATUS_RUNNING" || body.LayerOutputs["bg"] == "" {
			t.Fatalf("unexpected body: %#v", body)
		}
	})

	t.Run("cancel forbidden", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{})
		h.claimPipeline(t, "p-2003", testEmail)
		h.writeExecution(t, "p-2003", "executions/p-2003", "")
		res := h.do(t, "other@example.com", http.MethodPost, "/v1/jobs/p-2003/cancel", "")
		assertStatus(t, res, http.StatusForbidden)
		assertErrorContains(t, res, "not authorized")
	})

	t.Run("cancel pending returns conflict", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{})
		h.claimPipeline(t, "p-2004", testEmail)
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/p-2004/cancel", "")
		assertStatus(t, res, http.StatusConflict)
		assertErrorContains(t, res, "has not been submitted yet")
	})

	t.Run("cancel coordinator failure", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{
			cancelFn: func(context.Context, string) (*pb.CancelPipelineResponse, error) {
				return nil, errors.New("cancel denied")
			},
		})
		h.claimPipeline(t, "p-2005", testEmail)
		h.writeExecution(t, "p-2005", "executions/p-2005", "")
		res := h.do(t, testEmail, http.MethodPost, "/v1/jobs/p-2005/cancel", "")
		assertStatus(t, res, http.StatusBadGateway)
		assertErrorContains(t, res, "cancel denied")
	})
}

func TestHandleArtifactValidatesKindPathAndCompositeBucket(t *testing.T) {
	renderObject := fakestorage.Object{
		ObjectAttrs: fakestorage.ObjectAttrs{BucketName: testBucket, Name: "renders/p-3000/layer_main/frame_0001.exr"},
		Content:     []byte("exr"),
	}
	compositeObject := fakestorage.Object{
		ObjectAttrs: fakestorage.ObjectAttrs{BucketName: testBucket, Name: "custom/p-3000/output.mp4"},
		Content:     []byte("mp4"),
	}

	t.Run("path traversal rejected", func(t *testing.T) {
		h := newAPITestHarness(t, []fakestorage.Object{renderObject}, stubCoordinator{})
		h.claimPipeline(t, "p-3000", testEmail)
		res := h.do(t, testEmail, http.MethodGet, "/v1/jobs/p-3000/artifacts/render/%2E%2E/secret.exr", "")
		assertStatus(t, res, http.StatusBadRequest)
		assertErrorContains(t, res, "invalid path segment")
	})

	t.Run("bad composite bucket rejected", func(t *testing.T) {
		h := newAPITestHarness(t, nil, stubCoordinator{})
		h.claimPipeline(t, "p-3000", testEmail)
		h.writeExecution(t, "p-3000", "executions/p-3000", "gs://other-bucket/custom/p-3000/")
		res := h.do(t, testEmail, http.MethodGet, "/v1/jobs/p-3000/artifacts/composite/output.mp4", "")
		assertStatus(t, res, http.StatusBadRequest)
		assertErrorContains(t, res, "not readable by this API")
	})

	t.Run("render success", func(t *testing.T) {
		h := newAPITestHarness(t, []fakestorage.Object{renderObject}, stubCoordinator{})
		h.claimPipeline(t, "p-3000", testEmail)
		res := h.do(t, testEmail, http.MethodGet, "/v1/jobs/p-3000/artifacts/render/layer_main/frame_0001.exr", "")
		assertStatus(t, res, http.StatusOK)
		var body map[string]string
		decodeJSON(t, res, &body)
		if !strings.Contains(body["url"], "/renders/p-3000/layer_main/frame_0001.exr") {
			t.Fatalf("signed url = %q", body["url"])
		}
	})

	t.Run("composite success uses stored prefix", func(t *testing.T) {
		h := newAPITestHarness(t, []fakestorage.Object{compositeObject}, stubCoordinator{})
		h.claimPipeline(t, "p-3000", testEmail)
		h.writeExecution(t, "p-3000", "executions/p-3000", "gs://test-bucket/custom/p-3000/")
		res := h.do(t, testEmail, http.MethodGet, "/v1/jobs/p-3000/artifacts/composite/output.mp4", "")
		assertStatus(t, res, http.StatusOK)
		var body map[string]string
		decodeJSON(t, res, &body)
		if !strings.Contains(body["url"], "/custom/p-3000/output.mp4") {
			t.Fatalf("signed url = %q", body["url"])
		}
	})
}

type apiTestHarness struct {
	storage *storage.Client
	owner   *OwnerStore
	server  *Server
	mux     *http.ServeMux
}

func newAPITestHarness(t *testing.T, objects []fakestorage.Object, coordinator stubCoordinator) *apiTestHarness {
	t.Helper()
	fakeServer := fakestorage.NewServer(objects)
	t.Cleanup(fakeServer.Stop)

	storageClient := fakeServer.Client()
	t.Cleanup(func() { _ = storageClient.Close() })
	if err := storageClient.Bucket(testBucket).Create(context.Background(), "test-project", nil); err != nil && !strings.Contains(err.Error(), "already") {
		t.Fatalf("create bucket: %v", err)
	}

	if coordinator.submitFn == nil {
		coordinator.submitFn = func(context.Context, *pb.SubmitPipelineRequest) (*pb.SubmitPipelineResponse, error) {
			return &pb.SubmitPipelineResponse{PipelineId: "executions/default"}, nil
		}
	}
	if coordinator.statusFn == nil {
		coordinator.statusFn = func(context.Context, string) (*pb.GetPipelineStatusResponse, error) {
			return &pb.GetPipelineStatusResponse{Status: pb.GetPipelineStatusResponse_PIPELINE_STATUS_SUCCEEDED}, nil
		}
	}
	if coordinator.cancelFn == nil {
		coordinator.cancelFn = func(context.Context, string) (*pb.CancelPipelineResponse, error) {
			return &pb.CancelPipelineResponse{Success: true}, nil
		}
	}

	owner := NewOwnerStore(storageClient, testBucket)
	server := NewServer(
		Config{DataBucket: testBucket},
		storageClient,
		stubSigner{},
		owner,
		NewSceneValidator(storageClient, testBucket),
		coordinator,
		NewEmailLimiter(100, 100),
	)
	mux := http.NewServeMux()
	server.RegisterRoutes(mux)
	return &apiTestHarness{storage: storageClient, owner: owner, server: server, mux: mux}
}

func (h *apiTestHarness) do(t *testing.T, email, method, target, body string) *httptest.ResponseRecorder {
	t.Helper()
	req := httptest.NewRequest(method, target, bytes.NewBufferString(body))
	if email != "" {
		req = req.WithContext(context.WithValue(req.Context(), ctxKeyEmail, email))
	}
	rec := httptest.NewRecorder()
	h.mux.ServeHTTP(rec, req)
	return rec
}

func (h *apiTestHarness) claimPipeline(t *testing.T, pipelineID, email string) {
	t.Helper()
	if err := h.owner.Write(context.Background(), pipelineID, email); err != nil {
		t.Fatalf("claim pipeline: %v", err)
	}
}

func (h *apiTestHarness) writeExecution(t *testing.T, pipelineID, executionName, compositePrefix string) {
	t.Helper()
	if err := h.owner.WriteExecution(context.Background(), pipelineID, executionName, compositePrefix); err != nil {
		t.Fatalf("write execution marker: %v", err)
	}
}

func validSceneBundle(uploadPrefix string) []fakestorage.Object {
	return []fakestorage.Object{
		{
			ObjectAttrs: fakestorage.ObjectAttrs{BucketName: testBucket, Name: uploadPrefix + "/scene.json"},
			Content: []byte(`{
				"camera":{"look_from":[0,1,4],"look_at":[0,0,0],"vup":[0,1,0],"vfov":45},
				"animation":{"start":0,"end":1,"fps":24,"shutter_angle":180},
				"layers":["layers/bg.json"]
			}`),
		},
		{
			ObjectAttrs: fakestorage.ObjectAttrs{BucketName: testBucket, Name: uploadPrefix + "/layers/bg.json"},
			Content:     []byte(`{"animated":false}`),
		},
	}
}

func assertStatus(t *testing.T, res *httptest.ResponseRecorder, want int) {
	t.Helper()
	if res.Code != want {
		t.Fatalf("status = %d, want %d, body=%s", res.Code, want, res.Body.String())
	}
}

func assertErrorContains(t *testing.T, res *httptest.ResponseRecorder, want string) {
	t.Helper()
	var body map[string]string
	decodeJSON(t, res, &body)
	if !strings.Contains(body["error"], want) {
		t.Fatalf("error = %q, want substring %q", body["error"], want)
	}
}

func decodeJSON(t *testing.T, res *httptest.ResponseRecorder, dst any) {
	t.Helper()
	if err := json.Unmarshal(res.Body.Bytes(), dst); err != nil {
		t.Fatalf("decode json: %v\nbody=%s", err, res.Body.String())
	}
}
