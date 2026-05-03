package api

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"path"
	"strings"
	"time"

	"cloud.google.com/go/storage"
	"github.com/google/uuid"
	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

const (
	uploadURLTTL     = 15 * time.Minute
	artifactURLTTL   = 15 * time.Minute
	maxManifestFiles = 2048
	maxPathLength    = 512
)

// Config bundles all the wiring required by the API handlers.
type Config struct {
	DataBucket       string
	UploadRoot       string // "uploads"
	DefaultCompositePrefix string // "composites"
	DefaultRenderPrefix    string // "renders"
}

// Server hosts all the HTTP handlers and their runtime dependencies.
type Server struct {
	cfg         Config
	storage     *storage.Client
	signer      *URLSigner
	owner       *OwnerStore
	normalizer  *Normalizer
	coordinator *CoordinatorClient
	limiter     *EmailLimiter
}

func NewServer(
	cfg Config,
	storageClient *storage.Client,
	signer *URLSigner,
	owner *OwnerStore,
	normalizer *Normalizer,
	coordinator *CoordinatorClient,
	limiter *EmailLimiter,
) *Server {
	if cfg.UploadRoot == "" {
		cfg.UploadRoot = "uploads"
	}
	if cfg.DefaultCompositePrefix == "" {
		cfg.DefaultCompositePrefix = "composites"
	}
	if cfg.DefaultRenderPrefix == "" {
		cfg.DefaultRenderPrefix = "renders"
	}
	return &Server{
		cfg:         cfg,
		storage:     storageClient,
		signer:      signer,
		owner:       owner,
		normalizer:  normalizer,
		coordinator: coordinator,
		limiter:     limiter,
	}
}

// RegisterRoutes wires the API routes onto a ServeMux.
func (s *Server) RegisterRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /healthz", s.handleHealthz)
	mux.HandleFunc("POST /v1/jobs/init", s.handleInit)
	mux.HandleFunc("POST /v1/jobs/{id}/submit", s.handleSubmit)
	mux.HandleFunc("GET /v1/jobs/{id}", s.handleStatus)
	mux.HandleFunc("POST /v1/jobs/{id}/cancel", s.handleCancel)
	mux.HandleFunc("GET /v1/jobs/{id}/artifacts/{kind}/{name...}", s.handleArtifact)
}

// ── /healthz ─────────────────────────────────────────────────────────────

func (s *Server) handleHealthz(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

// ── /v1/jobs/init ────────────────────────────────────────────────────────

type initRequest struct {
	Files []initFile `json:"files"`
}

type initFile struct {
	Path        string `json:"path"`
	ContentType string `json:"content_type,omitempty"`
	Size        int64  `json:"size,omitempty"`
}

type initResponse struct {
	PipelineID      string            `json:"pipeline_id"`
	UploadPrefix    string            `json:"upload_prefix"`
	UploadURLs      map[string]string `json:"upload_urls"`
	UploadExpiresAt time.Time         `json:"upload_expires_at"`
}

func (s *Server) handleInit(w http.ResponseWriter, r *http.Request) {
	email := EmailFromContext(r.Context())
	if email == "" {
		httpError(w, http.StatusUnauthorized, "unauthenticated")
		return
	}
	if !s.limiter.AllowInit(email) {
		httpError(w, http.StatusTooManyRequests, "init rate limit exceeded")
		return
	}

	var req initRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		httpError(w, http.StatusBadRequest, "invalid JSON body: "+err.Error())
		return
	}
	if len(req.Files) == 0 {
		httpError(w, http.StatusBadRequest, "files must be non-empty")
		return
	}
	if len(req.Files) > maxManifestFiles {
		httpError(w, http.StatusBadRequest, fmt.Sprintf("too many files (>%d)", maxManifestFiles))
		return
	}

	seenScene := false
	for _, f := range req.Files {
		if len(f.Path) > maxPathLength {
			httpError(w, http.StatusBadRequest, fmt.Sprintf("path too long: %q", f.Path))
			return
		}
		cleaned, err := SanitizeObjectPath(f.Path)
		if err != nil {
			httpError(w, http.StatusBadRequest, err.Error())
			return
		}
		if strings.HasSuffix(cleaned, "scene.json") {
			seenScene = true
		}
	}
	if !seenScene {
		httpError(w, http.StatusBadRequest, "manifest must include a scene.json")
		return
	}

	pipelineID := "p-" + uuid.New().String()
	uploadPrefix := path.Join(s.cfg.UploadRoot, pipelineID)

	if err := s.owner.Write(r.Context(), pipelineID, email); err != nil {
		log.Printf("[API]: write owner marker: %v", err)
		httpError(w, http.StatusInternalServerError, "failed to claim pipeline id")
		return
	}

	expiresAt := time.Now().Add(uploadURLTTL)
	urls := make(map[string]string, len(req.Files))
	for _, f := range req.Files {
		cleaned, _ := SanitizeObjectPath(f.Path)
		object := path.Join(uploadPrefix, cleaned)
		u, err := s.signer.SignPut(s.cfg.DataBucket, object, f.ContentType, uploadURLTTL)
		if err != nil {
			log.Printf("[API]: sign PUT url: %v", err)
			httpError(w, http.StatusInternalServerError, "failed to sign upload URL")
			return
		}
		urls[cleaned] = u
	}

	writeJSON(w, http.StatusOK, initResponse{
		PipelineID:      pipelineID,
		UploadPrefix:    fmt.Sprintf("gs://%s/%s", s.cfg.DataBucket, uploadPrefix),
		UploadURLs:      urls,
		UploadExpiresAt: expiresAt,
	})
}

// ── /v1/jobs/{id}/submit ─────────────────────────────────────────────────

type submitRequest struct {
	ScenePath              string `json:"scene_path"`
	CompositeOutputURIPrefix string `json:"composite_output_uri_prefix"`
	EnableCache            bool   `json:"enable_cache"`
}

type submitResponse struct {
	PipelineID    string `json:"pipeline_id"`
	ExecutionName string `json:"execution_name"`
	SceneURI      string `json:"scene_uri"`
}

func (s *Server) handleSubmit(w http.ResponseWriter, r *http.Request) {
	email := EmailFromContext(r.Context())
	pipelineID := r.PathValue("id")
	if err := s.requireOwnership(r.Context(), pipelineID, email); err != nil {
		s.respondOwnershipError(w, err)
		return
	}
	if !s.limiter.AllowSubmit(email) {
		httpError(w, http.StatusTooManyRequests, "submit rate limit exceeded")
		return
	}

	var req submitRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil && !errors.Is(err, io.EOF) {
		httpError(w, http.StatusBadRequest, "invalid JSON body: "+err.Error())
		return
	}
	if req.ScenePath == "" {
		req.ScenePath = "scene.json"
	}

	uploadPrefix := path.Join(s.cfg.UploadRoot, pipelineID)

	sceneURI, err := s.normalizer.Normalize(r.Context(), uploadPrefix, req.ScenePath)
	if err != nil {
		log.Printf("[API]: normalize scene: %v", err)
		httpError(w, http.StatusBadRequest, "failed to normalize scene: "+err.Error())
		return
	}

	compositePrefix := req.CompositeOutputURIPrefix
	if compositePrefix == "" {
		compositePrefix = fmt.Sprintf("gs://%s/%s/%s/", s.cfg.DataBucket, s.cfg.DefaultCompositePrefix, pipelineID)
	}
	if !strings.HasPrefix(compositePrefix, "gs://") {
		httpError(w, http.StatusBadRequest, "composite_output_uri_prefix must start with gs://")
		return
	}

	resp, err := s.coordinator.Submit(r.Context(), &pb.SubmitPipelineRequest{
		PipelineId:               pipelineID,
		SceneUri:                 sceneURI,
		CompositeOutputUriPrefix: compositePrefix,
		EnableCache:              req.EnableCache,
	})
	if err != nil {
		log.Printf("[API]: coordinator.Submit: %v", err)
		httpError(w, http.StatusBadGateway, "coordinator rejected submission: "+err.Error())
		return
	}

	if err := s.owner.WriteExecution(r.Context(), pipelineID, resp.PipelineId); err != nil {
		log.Printf("[API]: write execution marker: %v", err)
		// Best-effort: the pipeline already started, just surface a warning.
	}

	writeJSON(w, http.StatusOK, submitResponse{
		PipelineID:    pipelineID,
		ExecutionName: resp.PipelineId,
		SceneURI:      sceneURI,
	})
}

// ── /v1/jobs/{id} (status) ────────────────────────────────────────────────

type statusResponse struct {
	PipelineID      string            `json:"pipeline_id"`
	Status          string            `json:"status"`
	ErrorMessage    string            `json:"error_message,omitempty"`
	LayerOutputs    map[string]string `json:"layer_outputs,omitempty"`
	CompositeOutput string            `json:"composite_output,omitempty"`
}

func (s *Server) handleStatus(w http.ResponseWriter, r *http.Request) {
	email := EmailFromContext(r.Context())
	pipelineID := r.PathValue("id")
	if err := s.requireOwnership(r.Context(), pipelineID, email); err != nil {
		s.respondOwnershipError(w, err)
		return
	}

	execName, err := s.owner.ReadExecution(r.Context(), pipelineID)
	if err != nil {
		if errors.Is(err, ErrPipelineNotFound) {
			httpError(w, http.StatusConflict, "pipeline has not been submitted yet")
			return
		}
		log.Printf("[API]: read execution marker: %v", err)
		httpError(w, http.StatusInternalServerError, "failed to read pipeline state")
		return
	}

	resp, err := s.coordinator.Status(r.Context(), execName)
	if err != nil {
		log.Printf("[API]: coordinator.Status: %v", err)
		httpError(w, http.StatusBadGateway, "coordinator status fetch failed: "+err.Error())
		return
	}

	writeJSON(w, http.StatusOK, statusResponse{
		PipelineID:      pipelineID,
		Status:          resp.Status.String(),
		ErrorMessage:    resp.ErrorMessage,
		LayerOutputs:    resp.LayerOutputs,
		CompositeOutput: resp.CompositeOutput,
	})
}

// ── /v1/jobs/{id}/cancel ─────────────────────────────────────────────────

func (s *Server) handleCancel(w http.ResponseWriter, r *http.Request) {
	email := EmailFromContext(r.Context())
	pipelineID := r.PathValue("id")
	if err := s.requireOwnership(r.Context(), pipelineID, email); err != nil {
		s.respondOwnershipError(w, err)
		return
	}

	execName, err := s.owner.ReadExecution(r.Context(), pipelineID)
	if err != nil {
		if errors.Is(err, ErrPipelineNotFound) {
			httpError(w, http.StatusConflict, "pipeline has not been submitted yet")
			return
		}
		log.Printf("[API]: read execution marker: %v", err)
		httpError(w, http.StatusInternalServerError, "failed to read pipeline state")
		return
	}

	resp, err := s.coordinator.Cancel(r.Context(), execName)
	if err != nil {
		log.Printf("[API]: coordinator.Cancel: %v", err)
		httpError(w, http.StatusBadGateway, "coordinator cancel failed: "+err.Error())
		return
	}

	writeJSON(w, http.StatusOK, map[string]bool{"success": resp.Success})
}

// ── /v1/jobs/{id}/artifacts/{kind}/{name} ─────────────────────────────────

// handleArtifact issues a short-lived signed GET URL for a render output
// artifact and 302-redirects the client to it. Supported kinds:
//   - "composite"  → gs://<data>/composites/<pipeline_id>/<name>
//   - "render"     → gs://<data>/renders/<pipeline_id>/<name>
//     (name may contain slashes, e.g. "layer_main/frame_0001.exr")
func (s *Server) handleArtifact(w http.ResponseWriter, r *http.Request) {
	email := EmailFromContext(r.Context())
	pipelineID := r.PathValue("id")
	if err := s.requireOwnership(r.Context(), pipelineID, email); err != nil {
		s.respondOwnershipError(w, err)
		return
	}

	kind := r.PathValue("kind")
	name := r.PathValue("name")
	cleaned, err := SanitizeObjectPath(name)
	if err != nil {
		httpError(w, http.StatusBadRequest, err.Error())
		return
	}

	var object string
	switch kind {
	case "composite":
		object = path.Join(s.cfg.DefaultCompositePrefix, pipelineID, cleaned)
	case "render":
		object = path.Join(s.cfg.DefaultRenderPrefix, pipelineID, cleaned)
	default:
		httpError(w, http.StatusBadRequest, fmt.Sprintf("unknown artifact kind %q", kind))
		return
	}

	u, err := s.signer.SignGet(s.cfg.DataBucket, object, artifactURLTTL)
	if err != nil {
		log.Printf("[API]: sign GET url: %v", err)
		httpError(w, http.StatusInternalServerError, "failed to sign artifact URL")
		return
	}
	http.Redirect(w, r, u, http.StatusFound)
}

// ── helpers ──────────────────────────────────────────────────────────────

func (s *Server) requireOwnership(ctx context.Context, pipelineID, email string) error {
	if pipelineID == "" || !strings.HasPrefix(pipelineID, "p-") {
		return ErrPipelineNotFound
	}
	return s.owner.Require(ctx, pipelineID, email)
}

func (s *Server) respondOwnershipError(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, ErrPipelineNotFound):
		httpError(w, http.StatusNotFound, "pipeline not found")
	case errors.Is(err, ErrPipelineForbidden):
		httpError(w, http.StatusForbidden, "not authorized for this pipeline")
	default:
		log.Printf("[API]: ownership check: %v", err)
		httpError(w, http.StatusInternalServerError, "ownership check failed")
	}
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func httpError(w http.ResponseWriter, status int, msg string) {
	writeJSON(w, status, map[string]string{"error": msg})
}

// CORSMiddleware allows the previewer origins to call the API. origins is a
// comma-separated list; "*" allows any origin (dev only).
func CORSMiddleware(originsCSV string) func(http.Handler) http.Handler {
	allowed := map[string]struct{}{}
	allowAny := false
	for _, o := range strings.Split(originsCSV, ",") {
		o = strings.TrimSpace(o)
		if o == "*" {
			allowAny = true
		}
		if o != "" {
			allowed[o] = struct{}{}
		}
	}
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			origin := r.Header.Get("Origin")
			if origin != "" {
				if allowAny {
					w.Header().Set("Access-Control-Allow-Origin", "*")
				} else if _, ok := allowed[origin]; ok {
					w.Header().Set("Access-Control-Allow-Origin", origin)
					w.Header().Set("Vary", "Origin")
				}
				w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
				w.Header().Set("Access-Control-Allow-Headers", "Authorization, Content-Type")
				w.Header().Set("Access-Control-Max-Age", "600")
			}
			if r.Method == http.MethodOptions {
				w.WriteHeader(http.StatusNoContent)
				return
			}
			next.ServeHTTP(w, r)
		})
	}
}
