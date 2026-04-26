# Coordinator

The Coordinator is the entry point for the Skewer system, deployed as a stateless **Cloud Run** service. It serves as the bridge between the user (CLI) and the serverless execution pipeline.

## Responsibilities

### 1. Job Validation & Submission

The Coordinator receives gRPC requests from the CLI and performs:

- **Scene Validation** - Checks that referenced assets exist in GCS.
- **Workflow Triggering** - Initiates a `skewer-render-pipeline` execution in **Cloud Workflows**.
- **Execution Tracking** - Maps user job IDs to internal GCP execution resource names.

### 2. Pipeline Configuration

The Coordinator translates high-level job parameters into the precise environment variables and Batch profiles needed by the pipeline:

- **Resource Selection** - Determines if the job requires Spot or Standard instances based on user flags.
- **Bucket Mapping** - Passes the correct GCS bucket URIs for data and caching to the workflow.

### 3. Monitoring & Progress

- **Status Aggregation** - Queries the Cloud Workflows and Cloud Batch APIs to provide a unified status back to the user.
- **Metadata Management** - Stores job metadata and completion status for future reference.

## Architecture

```
User CLI ─▶ Coordinator (Cloud Run) ─▶ Cloud Workflows ─▶ Cloud Batch
```

### Key Components

| Component | Platform | Description |
|-----------|----------|-------------|
| **gRPC Server** | Cloud Run | Handles `SubmitJob` and `GetJobStatus` RPCs. |
| **Workflow Client** | Google SDK | Triggers and monitors the managed orchestration DAG. |
| **IAM Integration** | Google Cloud | Uses Service Account impersonation to securely interact with Batch and Storage. |

## Deployment

The Coordinator is built via **Cloud Build** and deployed using **Terraform**.

```bash
# Example manual deploy (handled by CI/CD)
gcloud run deploy skewer-coordinator \
  --image gcr.io/PROJECT/skewer-coordinator \
  --env-vars DATA_BUCKET=my-bucket,WORKFLOW_NAME=render-pipeline
```

## Configuration

The Coordinator's behavior is primarily controlled via Environment Variables in the Cloud Run service:

| Variable | Description |
|----------|-------------|
| `DATA_BUCKET` | The GCS bucket where assets and renders are stored. |
| `CACHE_BUCKET` | The GCS bucket used for content-hash layer caching. |
| `WORKFLOW_NAME` | The full resource ID of the Cloud Workflow to trigger. |
| `GCP_PROJECT` | The project ID for API calls. |

## See Also

- [Architecture Overview](overview.md) - System-level architecture
- [GCP Deployment](../deployment/gcp.md) - Terraform and Infrastructure details
- [gRPC API](../api/grpc.md) - Protocol definitions
