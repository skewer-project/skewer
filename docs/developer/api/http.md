# HTTP API Reference

This document describes the HTTP API layer that clients interact with. The API sits between external clients (CLI, web previewer) and the internal gRPC `CoordinatorService`, providing authentication, rate limiting, ownership enforcement, scene normalization, and signed URL generation.

Located in: `orchestration/internal/api/`

## Architecture

```
Client ──► HTTP API (Cloud Run) ──► gRPC CoordinatorService ──► Cloud Workflows
```

### Middleware Stack

Each request passes through the following middleware in order:

1. **Authentication** — Firebase ID token validation
2. **Rate Limiting** — Per-email rate limiting
3. **Ownership** — Job ownership verification
4. **Scene Normalization** — Layer file upload and path resolution
5. **Signed URL Generation** — GCS signed URLs for direct upload

## Endpoints

### POST /v1/jobs/init

Initialize a new job and upload scene files.

```
POST /v1/jobs/init
Content-Type: multipart/form-data
Authorization: Bearer <firebase-id-token>
```

**Request (multipart/form-data):**

| Field | Type | Description |
|-------|------|-------------|
| `scene.json` | `file` | Scene manifest file **[Required]** |
| `files[]` | `file[]` | Additional layer/asset files **[Required]** |
| `frames` | `int` | Number of frames to render (default: 1) |
| `samples` | `int` | Override max samples per pixel |
| `width` | `int` | Override image width |
| `height` | `int` | Override image height |
| `deep` | `bool` | Enable deep EXR output |

**Limits:**
- Maximum 2048 files per request
- Paths must be ≤ 512 characters
- Paths cannot contain `..`

**Response:**

```json
{
    "job_id": "skw-abc123",
    "upload_urls": {
        "data/00_scene.json": "https://storage.googleapis.com/...",
        "data/01_layer_mercury.json": "https://storage.googleapis.com/..."
    }
}
```

### POST /v1/jobs/{id}/submit

Submit an initialized job for rendering.

```
POST /v1/jobs/{id}/submit
Authorization: Bearer <firebase-id-token>
```

**Request:**

```json
{
    "scene_path": "data/00_scene.json",
    "frames": 1,
    "overrides": {
        "samples": 256,
        "width": 1920,
        "height": 1080,
        "enable_deep": true
    }
}
```

**Rate Limit:** 5 requests per hour per email.

**Response:**

```json
{
    "job_id": "skw-abc123",
    "status": "JOB_STATUS_RUNNING",
    "progress": 0.0,
    "created_at": "2025-02-20T12:00:00Z",
    "updated_at": "2025-02-20T12:00:05Z"
}
```

### GET /v1/jobs/{id}

Get the current status of a job.

```
GET /v1/jobs/{id}
Authorization: Bearer <firebase-id-token>
```

**Response:**

```json
{
    "job_id": "skw-abc123",
    "status": "JOB_STATUS_RUNNING",
    "progress": 45.2,
    "layers": [
        {"name": "mercury", "status": "completed"},
        {"name": "venus", "status": "rendering", "progress": 62.0},
        {"name": "earth", "status": "pending"}
    ],
    "created_at": "2025-02-20T12:00:00Z",
    "updated_at": "2025-02-20T12:05:00Z"
}
```

### GET /v1/jobs

List all jobs for the authenticated user.

```
GET /v1/jobs
Authorization: Bearer <firebase-id-token>
```

**Response:**

```json
{
    "jobs": [
        {
            "job_id": "skw-abc123",
            "status": "JOB_STATUS_RUNNING",
            "progress": 45.2,
            "created_at": "2025-02-20T12:00:00Z"
        }
    ]
}
```

### POST /v1/jobs/{id}/cancel

Cancel a running job.

```
POST /v1/jobs/{id}/cancel
Authorization: Bearer <firebase-id-token>
```

**Response:**

```json
{
    "success": true,
    "message": "Job cancelled successfully"
}
```

## Authentication

All endpoints require a Firebase ID token in the `Authorization` header:

```
Authorization: Bearer <firebase-id-token>
```

The API verifies the token with Firebase Admin SDK and extracts the user's email for rate limiting and ownership.

### Authorization

- **Ownership:** Jobs are owned by the email extracted from the Firebase token. Only the owner can view or cancel their job. This is enforced via a `_owner.txt` file stored alongside the job's data.
- **Admin Access:** Emails listed in the `admin_emails` Terraform variable can view and cancel any job.

### Configuration

The following Terraform variables control the API:

| Variable | Default | Description |
|----------|---------|-------------|
| `admin_emails` | `[]` | Emails with admin access to all jobs |
| `previewer_authorized_domains` | `[]` | Allowed domains for the previewer |
| `api_rate_init_per_hour` | `60` | Max `/init` requests per hour per email |
| `api_rate_submit_per_hour` | `5` | Max `/submit` requests per hour per email |

## Error Responses

### 400 Bad Request

```json
{
    "error": "invalid_request",
    "message": "scene.json file is required"
}
```

### 401 Unauthorized

```json
{
    "error": "unauthenticated",
    "message": "Missing or invalid Firebase ID token"
}
```

### 403 Forbidden

```json
{
    "error": "forbidden",
    "message": "You do not own this job"
}
```

### 404 Not Found

```json
{
    "error": "not_found",
    "message": "Job not found"
}
```

### 429 Too Many Requests

```json
{
    "error": "rate_limited",
    "message": "Rate limit exceeded. Try again later."
}
```

Headers:

| Header | Description |
|--------|-------------|
| `X-RateLimit-Limit` | Maximum requests per hour |
| `X-RateLimit-Remaining` | Remaining requests in current window |
| `Retry-After` | Seconds until the rate limit resets |

## See Also

- [gRPC API](grpc.md) - Internal service definitions
- [Coordinator Architecture](coordinator.md) - Service internals and deployment
- [GCP Deployment](../../getting-started/gcp.md) - Terraform configuration
