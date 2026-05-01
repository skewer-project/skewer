# GCP Deployment Guide (Serverless)

This guide covers the deployment of the Skewer serverless architecture on Google Cloud Platform using Terraform, Cloud Build, and managed services.

## Architecture Overview

The production environment consists of:
- **Cloud Run**: Hosts the Go Coordinator.
- **Cloud Workflows**: Orchestrates the rendering DAG.
- **Cloud Batch**: Executes C++ worker containers on dynamic VM groups.
- **Artifact Registry**: Stores versioned Docker images.
- **GCS Buckets**: `data` (assets/renders) and `cache` (intermediate layers).

## Prerequisites

- [Terraform](https://www.terraform.io/) (>= 1.6)
- Google Cloud SDK (`gcloud`)
- A GCP Project with billing enabled

## Infrastructure as Code (Terraform)

The infrastructure is defined in `deployments/terraform/`.

### 1. Initialize Backend
Create a GCS bucket for Terraform state and update `main.tf`:

```hcl
terraform {
  backend "gcs" {
    bucket = "your-tf-state-bucket"
    prefix = "skewer"
  }
}
```

### 2. Apply Configuration

```bash
cd deployments/terraform
terraform init
terraform apply -var="project_id=YOUR_PROJECT_ID" -var="region=us-central1"
```

This will provision:
- VPC Network and Subnets
- Cloud Run service (with a public hello-world placeholder)
- Cloud Workflows (`skewer-render-pipeline`)
- Artifact Registry repository
- GCS Buckets for data and caching
- IAM Service Accounts and minimal-privilege roles

## CI/CD Pipeline (Cloud Build)

The project uses Cloud Build to automate image creation and deployment.

### 1. Build and Push
The `deployments/cloudbuild.yaml` file defines the build steps:
- Multi-stage Docker builds for `coordinator`, `skewer`, and `loom`.
- Pushes images to Artifact Registry.
- Updates the Cloud Run Coordinator service with the new image.

### 2. Triggering a Build
A Cloud Build trigger is automatically created by Terraform. It watches the `main` branch of your repository.

```bash
# To trigger manually:
gcloud builds submit --config deployments/cloudbuild.yaml --substitutions=_AR_BASE=us-west2-docker.pkg.dev/PROJECT/skewer,_REGION=us-west2
```

## Storage & GCS FUSE

Workers access data via **GCS FUSE**. This is configured in the Cloud Batch job definition within `render_pipeline.yaml`:

- **Data Bucket**: Mounted to `/mnt/data/` (Read/Write)
- **Cache Bucket**: Mounted to `/mnt/cache/` (Read/Write)

**Note:** GCS FUSE is a high-latency filesystem. Workers are optimized to perform large, sequential reads and writes to minimize overhead.

## Cost Optimization

- **Spot Instances**: Skewer render workers default to `provisioningModel: "SPOT"` to reduce costs by up to 80%.
- **Lifecycle Rules**: GCS buckets have lifecycle rules (defined in `storage.tf`) to automatically delete old renders and cache layers after 30-90 days.
- **Scale-to-Zero**: The Cloud Run Coordinator and Cloud Batch VMs consume zero resources when no jobs are active.

## Monitoring

- **Cloud Logging**: All worker logs and Coordinator logs are available in the GCP Console.
- **Cloud Workflows Dashboard**: Visualize current and past pipeline executions.
- **Cloud Batch Job List**: Monitor VM provisioning and task progress.

## See Also

- [Architecture Overview](../architecture/overview.md)
- [Coordinator](../architecture/coordinator.md)
- [Local Deployment](local.md)
