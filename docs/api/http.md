# HTTP API Reference

This document describes the HTTP API layer that clients interact with. The API sits between external clients (CLI, web previewer) and the internal gRPC `CoordinatorService`, providing authentication, rate limiting, ownership enforcement, scene validation, and signed URL generation.

Located in: `orchestration/internal/api/`

## Architecture

```
Client ──► HTTP API (Cloud Run) ──► gRPC CoordinatorService ──► Cloud Workflows
              │
              ├── AuthVerifier (Firebase ID token)
              ├── CORSMiddleware
              ├── EmailLimiter (rate limiting)
              ├── OwnerStore (pipeline ownership)
              ├── SceneValidator (scene/layer metadata validation)
              ├── URLSigner (V4-signed GCS URLs)
              └── CoordinatorClient (gRPC wrapper)
```

The HTTP API is a Go `http.ServeMux` server that proxies requests to the coordinator via gRPC while enforcing security and business logic at each layer.

---

## Endpoints

### `GET /healthz`

Liveness probe. Skips authentication.

**Response:** `200 OK`
```json
{ "status": "ok" }
```

---

### `POST /v1/jobs/init`

Claims a new pipeline ID and returns V4-signed PUT URLs for uploading scene files.

**Requires:** Authenticated request. Rate limited (init bucket).

**Request body:**
```json
{
  "files": [
    { "path": "scene.json", "content_type": "application/json", "size": 1024 },
    { "path": "layer_main.json", "content_type": "application/json" },
    { "path": "assets/texture.png", "content_type": "image/png" }
  ]
}
```

**Validation:**

- `files` must be non-empty and contain at most 2048 entries
- At least one file must be named `scene.json`
- Each path must be ≤ 512 characters and pass path sanitization (no `..`, no leading `/`, no empty segments)

**Response:** `200 OK`
```json
{
  "pipeline_id": "p-a1b2c3d4-...",
  "upload_prefix": "gs://bucket/uploads/p-a1b2c3d4-...",
  "upload_urls": {
    "scene.json": "https://storage.googleapis.com/...?X-Goog-...",
    "layer_main.json": "https://storage.googleapis.com/...?X-Goog-..."
  },
  "upload_expires_at": "2024-01-15T12:00:00Z"
}
```

Upload URLs expire after 15 minutes. After uploading, call `/v1/jobs/{id}/submit`.

---

### `POST /v1/jobs/{id}/submit`

Validates the uploaded scene bundle and submits it to the coordinator for rendering.

**Requires:** Authenticated request + pipeline ownership. Rate limited (submit bucket).

**Request body:**
```json
{
  "scene_path": "scene.json",
  "composite_output_uri_prefix": "gs://bucket/composites/p-a1b2c3d4-...",
  "enable_cache": true
}
```

**Fields:**

- `scene_path` (optional, defaults to `"scene.json"`) — relative path within the upload prefix
- `composite_output_uri_prefix` (optional) — GCS prefix for final composited frames; defaults to `gs://<bucket>/composites/<pipeline_id>/`
- `enable_cache` (optional, defaults to `false`) — enable content-hash layer caching

**What happens:**

1. Verifies caller owns the pipeline
2. Reads uploaded `scene.json` and all referenced layer/context files from GCS
3. Requires explicit top-level `animation.start`, `animation.end`, positive `animation.fps`, and `animation.shutter_angle` in `(0, 360]`
4. Requires `animation.end >= animation.start`
5. Requires every referenced layer/context file to contain an explicit boolean `animated` field
6. Rejects absolute, traversal, and `gs://` layer/context references
7. Submits the original uploaded `scene.json` URI to the coordinator via gRPC

**Response:** `200 OK`
```json
{
  "pipeline_id": "p-a1b2c3d4-...",
  "execution_name": "exec-xyz",
  "scene_uri": "gs://bucket/uploads/p-a1b2c3d4-.../scene.json"
}
```

---

### `GET /v1/jobs/{id}`

Retrieves the current status of a submitted pipeline.

**Requires:** Authenticated request + pipeline ownership.

**Response:** `200 OK`
```json
{
  "pipeline_id": "p-a1b2c3d4-...",
  "status": "PIPELINE_STATUS_RUNNING",
  "layer_outputs": {
    "layer_main": "gs://bucket/renders/p-a1b2c3d4-.../layer_main"
  },
  "composite_output": "gs://bucket/composites/p-a1b2c3d4-..."
}
```

**Status values:**

| Value | Meaning |
| :------- | :--------- |
| `PIPELINE_STATUS_UNSPECIFIED` | Unknown state |
| `PIPELINE_STATUS_RUNNING` | Pipeline is executing |
| `PIPELINE_STATUS_SUCCEEDED` | All frames completed |
| `PIPELINE_STATUS_FAILED` | Error occurred |
| `PIPELINE_STATUS_CANCELLED` | Pipeline was cancelled |

---

### `POST /v1/jobs/{id}/cancel`

Cancels a running pipeline.

**Requires:** Authenticated request + pipeline ownership.

**Response:** `200 OK`
```json
{ "success": true }
```

---

### `GET /v1/jobs/{id}/artifacts/{kind}/{name}`

Issues a short-lived signed GET URL for a render output artifact and 302-redirects the client to it.

**Requires:** Authenticated request + pipeline ownership.

**Path parameters:**

- `kind` — `"composite"` or `"render"`
- `name` — artifact path within the prefix (may contain slashes, e.g. `layer_main/frame_0001.exr`)

**Resolution:**

- `composite` → `gs://<bucket>/composites/<pipeline_id>/<name>`
- `render` → `gs://<bucket>/renders/<pipeline_id>/<name>`

**Response:** `302 Found` — redirects to a V4-signed GET URL (15 min TTL)

---

## Middleware Chain

All requests (except `/healthz` and `OPTIONS` preflights) pass through the following middleware stack:

### 1. CORS Middleware

Configurable origin allowlist. Set via comma-separated list; `"*"` allows any origin (dev only).

```go
CORSMiddleware("https://previewer.skewer.io,http://localhost:5173")
```

**Headers set:**

- `Access-Control-Allow-Origin` — matched origin or `*`
- `Access-Control-Allow-Methods` — `GET, POST, OPTIONS`
- `Access-Control-Allow-Headers` — `Authorization, Content-Type`
- `Access-Control-Max-Age` — `600` (10 minutes)

OPTIONS preflight requests receive `204 No Content` immediately.

---

### 2. Authentication (`AuthVerifier`)

Validates Firebase ID tokens on every request.

**How it works:**

1. Extracts `Bearer <token>` from the `Authorization` header
2. Verifies the token against Firebase Auth
3. Checks that the token's `email` claim is verified
4. Checks the email against an admin allowlist (comma-separated, case-insensitive)
5. Injects the email into the request context for downstream middleware

See [GCP Deployment (Production)](../deployment/gcp.md#step-3-set-up-firebase-authentication) for more details on configuring authentication.

```go
verifier, _ := NewAuthVerifier(ctx, projectID, "admin@example.com,user@example.com")
```

**Failure modes:**

| Condition | Response |
| :----------- | :---------- |
| Missing/malformed Bearer token | `401 Unauthorized` |
| Invalid/expired token | `401 Unauthorized` |
| Unverified email | `403 Forbidden` |
| Email not in allowlist | `403 Forbidden` |

If the allowlist is empty, **all** authenticated emails are rejected (fail-closed).

**Accessing the email in downstream code:**
```go
email := api.EmailFromContext(r.Context())
```

---

### 3. Rate Limiting (`EmailLimiter`)

Enforces two per-email token buckets:

| Bucket | Purpose | Default limit |
| :-------- | :--------- | :--------------- |
| `init` | Signed URL issuance (`/v1/jobs/init`) | Configurable (higher) |
| `submit` | Pipeline submission (`/v1/jobs/{id}/submit`) | Configurable (lower) |

```go
limiter := NewEmailLimiter(100, 10) // 100 init/hr, 10 submit/hr
```

**How it works:**

- Token bucket algorithm using `golang.org/x/time/rate`
- Rate = `limit / 3600` tokens per second, burst = `limit`
- Stale entries evicted after 2 hours of inactivity via `Sweep()`

**Response on limit exceeded:** `429 Too Many Requests`

---

### 4. Ownership (`OwnerStore`)

Enforces that only the pipeline creator can access status, cancel, or artifact endpoints.

**How it works:**

- On `/init`: writes `uploads/{pipeline_id}/_owner.txt` with the caller's email (atomic — rejects if already exists)
- On `/submit`, `/status`, `/cancel`, `/artifacts`: reads `_owner.txt` and compares with the caller's email

```go
store := NewOwnerStore(storageClient, dataBucket)
```

**Additional markers:**

- `_execution.txt` — stores the Cloud Workflows execution name after a successful submit

**Failure modes:**

| Condition | Response |
| :----------- | :---------- |
| Pipeline ID missing or doesn't start with `p-` | `404 Not Found` |
| `_owner.txt` not found | `404 Not Found` |
| Email doesn't match owner | `403 Forbidden` |

---

## Components

### Scene Validator (`SceneValidator`)

Validates that uploaded bundles already contain the animation metadata expected by the coordinator. It does not rewrite uploaded scene files.

```go
validator := NewSceneValidator(storageClient, dataBucket)
sceneURI, err := validator.Validate(ctx, uploadPrefix, scenePath)
```

**What it does:**

1. Downloads `scene.json` from GCS
2. Verifies `layers` is a non-empty string array and `context`, when present, is a string array
3. Verifies `animation.start`, `animation.end`, `animation.fps`, and `animation.shutter_angle` are numbers
4. Verifies `animation.fps` is positive, `animation.end >= animation.start`, and `animation.shutter_angle` is in `(0, 360]`
5. Rejects absolute, traversal, and `gs://` layer/context references before reading referenced objects
6. Verifies each referenced layer/context JSON has an explicit boolean `animated` field
7. Returns the original `gs://` URI for the uploaded scene

---

### Signed URLs (`URLSigner`)

Produces V4-signed GCS URLs via the IAM SignBlob API — no private key file needed.

```go
signer, _ := NewURLSigner(ctx, "api@project.iam.gserviceaccount.com")

// Upload URL (15 min TTL)
putURL, _ := signer.SignPut(bucket, "uploads/p-abc/scene.json", "application/json", 15*time.Minute)

// Download URL (15 min TTL)
getURL, _ := signer.SignGet(bucket, "composites/p-abc/frame_0001.exr", 15*time.Minute)
```

**IAM requirement:** The service account must have `roles/iam.serviceAccountTokenCreator` on itself so that SignBlob calls succeed using metadata-server credentials.

**Path sanitization:** `SanitizeObjectPath()` rejects:

- Empty paths
- Leading `/` (absolute paths)
- Path traversal segments (`..`, `.`, empty segments)

---

### Coordinator Client (`CoordinatorClient`)

Thin gRPC wrapper that dials the `CoordinatorService` on Cloud Run with Google ID-token authentication.

```go
client, _ := NewCoordinatorClient(ctx, "https://skewer-coordinator-xxx.run.app")

resp, err := client.Submit(ctx, &pb.SubmitPipelineRequest{
    PipelineId:               pipelineID,
    SceneUri:                 sceneURI,
    CompositeOutputUriPrefix: compositePrefix,
    EnableCache:              true,
})
```

**Authentication:** Uses `idtoken.NewTokenSource` with the coordinator URL as audience. On Cloud Run this works out of the box using metadata-server credentials. The API service account needs `roles/run.invoker` on the coordinator service.

**TLS:** Connects to port `:443` with `credentials.NewTLS`.

---

### Server Configuration

```go
cfg := api.Config{
    DataBucket:             "skewer-data-bucket",
    UploadRoot:             "uploads",         // default
    DefaultCompositePrefix: "composites",      // default
    DefaultRenderPrefix:    "renders",         // default
}

server := api.NewServer(cfg, storageClient, signer, ownerStore, validator, coordinatorClient, limiter)

mux := http.NewServeMux()
server.RegisterRoutes(mux)

// Wrap with middleware chain
handler := limiter.Middleware(selectBucket)(
    verifier.Middleware("/healthz")(
        api.CORSMiddleware("https://previewer.skewer.io")(mux),
    ),
)
```
