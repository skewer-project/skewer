# Rethink Cloud Architecture: GCP-Native Distributed Rendering

## Context

The current K8s setup has three major pain points:
1. **Logs are invisible** — no structured logging, no Cloud Logging integration; debugging requires `kubectl logs` hunting
2. **Extracting rendered images is painful** — HostPath/PVC storage means spinning up reader pods to pull files out
3. **Path handling is fragile** — `translateLocalPath()` in `server.go:358-402` juggles three modes (GCS URIs, host absolute paths, repo-relative paths), causing edge-case failures

The root cause is that the architecture was built for local Minikube first, with GCP bolted on. This plan replaces it with a GCP-native design.

---

## Recommended Architecture: GKE Autopilot + Cloud Run Jobs + GCS

### What changes

| Component | Current | Proposed |
|-----------|---------|----------|
| **Coordinator** | K8s Deployment (Minikube/GKE) | GKE Autopilot Deployment |
| **Workers** | K8s Deployments scaled by coordinator via RBAC | **Cloud Run Jobs** launched per-task |
| **Storage** | HostPath volumes / optional GCS | **GCS-only** (via GCSFuse mounts) |
| **Logging** | `log.Printf` / `std::cout` | Structured JSON → Cloud Logging |
| **Auth** | Manual service account JSON in K8s Secret | **Workload Identity** (no key files) |
| **Images** | Local Docker builds loaded into Minikube | **Artifact Registry** |
| **Worker dispatch** | Pull-based gRPC streaming (`GetWorkStream`) | **Coordinator pushes** by launching Cloud Run Job executions |

### Why Cloud Run Jobs for workers

Workers currently run an infinite gRPC pull loop (`worker_loop.cc`), connecting to the coordinator, getting one task, rendering, reporting back, reconnecting. This requires:
- Coordinator managing K8s Deployments via RBAC (`cloud.go:82-200`)
- A capacity manager polling every 5s (`server.go:303-347`)
- A sweeper for dead worker detection (`scheduler.go`)
- Workers running continuously even when idle

With Cloud Run Jobs, the coordinator **launches a job execution per task**. Each execution: reads task params from env vars → renders via GCSFuse-mounted paths → writes output to GCS → exits. Cloud Run handles retries, timeouts, and logging natively.

**Key insight**: The C++ workers use POSIX file I/O throughout (`LoadSceneFromFile`, `session.Save()`). GCSFuse mounts GCS as a local filesystem, so **zero C++ file I/O code needs to change**.

---

## Implementation Steps

### Phase 1: GCS-Only Storage + Path Simplification

**Goal**: Eliminate PVCs and the complex path translation.

1. **`internal/coordinator/server.go`** — Replace `translateLocalPath()` (lines 358-402) with:
   ```go
   func gcsURIToFusePath(uri string) (string, error) {
       if !strings.HasPrefix(uri, "gs://") {
           return "", fmt.Errorf("cloud mode requires gs:// URIs, got: %s", uri)
       }
       return "/gcs/" + strings.TrimPrefix(uri, "gs://"), nil
   }
   ```
   - Remove `localStorageBase` field from `Server` struct
   - Update `handleRenderJobSubmit()` and `handleCompositeJobSubmit()` to use `gcsURIToFusePath()`

2. **`apps/cli/cmd/submit.go`** — Add scene upload to GCS:
   - New `--upload` flag: uploads local scene directory to `gs://bucket/scenes/` before submission
   - Validate `--scene` and `--output` are `gs://` URIs (or auto-prefix after upload)
   - Use `cloud.google.com/go/storage` for upload (already a Go dependency)

3. **`apps/coordinator/main.go`** — Remove `LOCAL_STORAGE_BASE` and `LOCAL_DATA_PATH` env var handling

### Phase 2: Structured Logging → Cloud Logging

**Goal**: All logs visible in Cloud Logging with structured fields.

4. **Go coordinator** — Switch from `log.Printf` to `log/slog` with JSON handler:
   - `apps/coordinator/main.go`: Initialize `slog.SetDefault(slog.New(slog.NewJSONHandler(os.Stdout, nil)))`
   - `internal/coordinator/server.go`: Replace all `log.Printf("[COORDINATOR] ...")` → `slog.Info("...", "job_id", jobID, ...)`
   - `internal/coordinator/scheduler.go`: Same treatment
   - `internal/coordinator/cloud.go`: Same treatment

5. **C++ workers** — Add a minimal JSON log helper (optional; `std::cout` already captured by Cloud Run):
   - New utility in `libs/worker/include/coordinator_worker/log.h`
   - Emits `{"severity":"INFO","message":"...","task_id":"..."}` to stdout
   - GKE and Cloud Run both auto-ingest JSON stdout as structured Cloud Logging entries

### Phase 3: Cloud Run Jobs for Workers

**Goal**: Replace the pull-based gRPC worker loop with coordinator-launched Cloud Run Jobs.

6. **New `CloudManager` interface** — Rewrite `internal/coordinator/cloud.go`:
   ```go
   type CloudManager interface {
       LaunchTask(ctx context.Context, workerType string, taskID string, env map[string]string) error
       CancelTask(ctx context.Context, taskID string) error
       ProvisionStorage(ctx context.Context, bucketName string) error
   }
   ```
   - New `CloudRunManager` struct using `cloud.google.com/go/run/apiv2` (Cloud Run Admin API)
   - `LaunchTask()`: creates a Cloud Run Job execution with task params as env vars + GCSFuse volume mount
   - Remove all `k8s.io/client-go` imports and K8s Deployment management code

7. **New cloud worker entrypoints** (simple, ~50 lines each):
   - `skewer/apps/cloud_worker/main.cc`: Reads `SCENE_URI`, `OUTPUT_URI`, `WIDTH`, `HEIGHT`, `MAX_SAMPLES`, etc. from env vars → calls `RenderSession` → exits
   - `loom/apps/cloud_worker/main.cc`: Reads `LAYER_URIS`, `OUTPUT_URI` from env vars → calls compositor → exits
   - These replace the gRPC pull loop with a direct "run once and exit" model
   - Add CMake targets for `skewer-cloud-worker` and `loom-cloud-worker`

8. **Task completion reporting** — Two options (recommend option A):
   - **A) Workers call `ReportTaskResult` gRPC**: Cloud Run Job worker calls coordinator's gRPC endpoint on completion (keep existing RPC, workers get coordinator address from env var)
   - **B) Coordinator polls Cloud Run Job status**: Simpler worker code but adds polling latency

9. **Remove pull-based dispatch from coordinator**:
   - `server.go`: Remove `GetWorkStream()` handler, remove `runCapacityManager()` goroutine
   - `scheduler.go`: Remove `skewerQueue`/`loomQueue` channels, remove `GetNextTask()`, remove `StartSweeper()`
   - Keep `activeTasks` map for tracking in-flight Cloud Run executions
   - Modify `EnqueueTask()` → instead of channel send, call `manager.LaunchTask()` directly
   - `coordinator.proto`: Remove `GetWorkStream` RPC and `GetWorkStreamRequest` message

### Phase 4: Workload Identity + Artifact Registry

**Goal**: No more service account JSON keys or manual Docker image loading.

10. **Artifact Registry**:
    - Create repo: `gcloud artifacts repositories create skewer --repository-format=docker --location=us-central1`
    - Update Dockerfiles to tag images as `us-central1-docker.pkg.dev/{project}/skewer/{name}:latest`
    - `Dockerfile.skewer` / `Dockerfile.loom`: Add cloud worker binary as entrypoint variant

11. **Workload Identity**:
    - Create GSAs: `skewer-coordinator` (needs `roles/run.developer` + `roles/storage.admin`), `skewer-worker` (needs `roles/storage.objectUser`)
    - Annotate K8s ServiceAccount for coordinator pod
    - Set Cloud Run Job service account to `skewer-worker` GSA
    - Remove `credentialsFile` from `NewK8sCloudManager`, remove `GOOGLE_APPLICATION_CREDENTIALS` env var, remove `skewer-gcp-creds` Secret from all manifests

### Phase 5: Deployment Automation

12. **`deployments/deploy_cloud.sh`** — New script:
    - Creates GKE Autopilot cluster, Artifact Registry, GCS bucket (one-time)
    - Sets up Workload Identity bindings
    - Builds & pushes images to AR
    - Deploys coordinator to GKE (`kubectl apply`)
    - Creates Cloud Run Job definitions with GCSFuse volume mounts

13. **`deployments/k8s/coordinator-cloud.yaml`** — New manifest:
    - No HostPath volumes, no credential secrets
    - Workload Identity annotation on ServiceAccount
    - Cloud Run IAM role for launching jobs

14. **Keep `deploy_local.sh`** — Local dev path stays unchanged (Minikube + HostPath + gRPC pull workers)

---

## What Gets Removed

- `GetWorkStream` RPC + streaming infrastructure in `server.go`
- `worker_loop.cc` / `worker_loop.h` (not needed for cloud; kept for local dev)
- `runCapacityManager()` goroutine
- `EnsureCapacity()` and all K8s Deployment CRUD in `cloud.go`
- `translateLocalPath()` and local/relative path handling
- Channel-based queues (`skewerQueue`, `loomQueue`) in `scheduler.go`
- Sweeper logic in `scheduler.go`
- HostPath volumes in cloud manifests
- `skewer-gcp-creds` Secret and credential file mounting
- RBAC for Deployment management

## What Gets Kept

- Job DAG logic (`job.go`, `dag.go`, `JobTracker`)
- `SubmitJob`, `GetJobStatus`, `CancelJob`, `ReportTaskResult` RPCs
- All C++ rendering and compositing engine code (unchanged)
- Proto message definitions for `RenderTask`, `CompositeTask`
- CLI command structure (cobra)
- Local development path (`deploy_local.sh` + Minikube)

---

## Verification

1. **Unit**: Run existing Go tests for coordinator after refactor
2. **Local smoke test**: `deploy_local.sh` still works (local path preserved)
3. **Cloud E2E**:
   - Upload a test scene to GCS via CLI: `skewer-cli submit --scene gs://bucket/scenes/cornell.json --output gs://bucket/renders/test/`
   - Verify Cloud Run Job launches in GCP Console
   - Check Cloud Logging for structured coordinator + worker logs
   - Download rendered image: `gcloud storage cp gs://bucket/renders/test/frame-0001.png .`
4. **DAG test**: Submit render job + dependent composite job, verify sequential execution

---

## Critical Files

| File | Change |
|------|--------|
| `internal/coordinator/cloud.go` | **Rewrite**: K8sCloudManager → CloudRunManager |
| `internal/coordinator/server.go` | Remove GetWorkStream, capacity manager, translateLocalPath; add slog |
| `internal/coordinator/scheduler.go` | Remove queues/sweeper; become dispatch-only tracker |
| `apps/coordinator/main.go` | Init slog, CloudRunManager, remove local storage env |
| `api/proto/coordinator/v1/coordinator.proto` | Remove GetWorkStream RPC |
| `skewer/apps/cloud_worker/main.cc` | **New**: env-var-driven render-once-and-exit |
| `loom/apps/cloud_worker/main.cc` | **New**: env-var-driven composite-once-and-exit |
| `apps/cli/cmd/submit.go` | Add GCS upload, validate gs:// URIs |
| `deployments/deploy_cloud.sh` | **New**: full GCP setup script |
| `deployments/k8s/coordinator-cloud.yaml` | **New**: cloud-mode coordinator manifest |
