# GKE Deployment (DEPRECATED)

> [!CAUTION]
> **This deployment model is DEPRECATED.** The Skewer project has migrated to a serverless architecture using Cloud Run, Cloud Workflows, and Cloud Batch. Please refer to the [GCP Deployment Guide](../deployment/gcp.md) for the current production architecture.

Deploy Skewer to Google Kubernetes Engine for production workloads.

## Prerequisites

- Google Cloud SDK (`gcloud`)
- kubectl
- Docker
- A GCP project with GKE enabled

## Setup

### 1. Configure GCP

```bash
# Set your project
gcloud config set project <PROJECT_ID>

# Enable required APIs
gcloud services enable container.googleapis.com artifactregistry.googleapis.com
```

### 2. Create Cluster

```bash
gcloud container clusters create skewer-cluster \
  --num-nodes=3 \
  --machine-type=n1-standard-4 \
  --region=us-central1
```

### 3. Build and Push Images

```bash
# Build coordinator
docker build -t gcr.io/<PROJECT>/coordinator:latest -f deployments/docker/Dockerfile.coordinator .

# Build workers
docker build -t gcr.io/<PROJECT>/skewer-worker:latest -f deployments/docker/Dockerfile.skewer .
docker build -t gcr.io/<PROJECT>/loom-worker:latest -f deployments/docker/Dockerfile.loom .

# Push to GCR
docker push gcr.io/<PROJECT>/coordinator:latest
docker push gcr.io/<PROJECT>/skewer-worker:latest
docker push gcr.io/<PROJECT>/loom-worker:latest
```

## Deploy

```bash
# Apply Kubernetes manifests
kubectl apply -f deployments/k8s/coordinator.yaml
kubectl apply -f deployments/k8s/skewer-worker.yaml
kubectl apply -f deployments/k8s/loom-worker.yaml
```

## Configuration

Edit the deployment manifests to update:
- Image names (`gcr.io/<PROJECT>/...`)
- Resource limits (CPU, memory)
- Replica counts

## Scaling

```bash
# Scale workers
kubectl scale deployment skewer-worker --replicas=10
kubectl scale deployment loom-worker --replicas=2
```

## Monitoring

```bash
# View logs
kubectl logs -l app=coordinator
kubectl logs -l app=skewer-worker

# Check status
kubectl get pods
kubectl describe pod <pod-name>
```

## Cloud Storage Integration

Workers read/write to GCS using `gs://` URIs. Ensure:
1. Service account has Storage Object Admin role
2. Workload Identity is enabled on the cluster

## See Also

- [Local Deployment](local.md) - Local development setup
- [Architecture Overview](../architecture/overview.md) - System design
