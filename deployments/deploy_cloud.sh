#!/usr/bin/env bash

# Deployment for GCP-Native Cloud Render
# Reuses existing Artifact Registry, GCS Bucket, and GSAs

set -e

# Configuration
PROJECT_ID="deft-sight-487822-q9"
REGION="us-west2"
REPOSITORY="skewer"
BUCKET="skewer-dev-aksads"
AR_URI="${REGION}-docker.pkg.dev/${PROJECT_ID}/${REPOSITORY}"

SKIP_WORKERS=false
if [[ "$1" == "--skip-workers" ]]; then
  SKIP_WORKERS=true
fi

echo "[DEPLOY] Using GCP Project: ${PROJECT_ID}"
echo "[DEPLOY] Region: ${REGION}"
echo "[DEPLOY] Artifact Registry: ${AR_URI}"
echo "[DEPLOY] GCS Bucket: ${BUCKET}"

# Move to the project root directory
cd "$(dirname "$0")/.."

# 1. Build and Push Images
echo "[DEPLOY] Building and pushing Docker images..."

# Build Coordinator
docker build --platform linux/amd64 -t "${AR_URI}/coordinator:latest" -f deployments/docker/Dockerfile.coordinator .
docker push "${AR_URI}/coordinator:latest"

if [ "$SKIP_WORKERS" = false ]; then
  # Build Skewer Worker
  docker build --platform linux/amd64 -t "${AR_URI}/skewer-worker:latest" -f deployments/docker/Dockerfile.skewer .
  docker push "${AR_URI}/skewer-worker:latest"

  # Build Loom Worker
  docker build --platform linux/amd64 -t "${AR_URI}/loom-worker:latest" -f deployments/docker/Dockerfile.loom .
  docker push "${AR_URI}/loom-worker:latest"
else
  echo "[DEPLOY] Skipping worker builds."
fi

# 2. Update/Create Cloud Run Jobs
# (The jobs were created in Phase 4; this ensures they are up to date)
echo "[DEPLOY] Updating Cloud Run Jobs..."

gcloud run jobs update skewer-worker \
  --image="${AR_URI}/skewer-worker:latest" \
  --region="${REGION}" \
  --service-account="skewer-worker@${PROJECT_ID}.iam.gserviceaccount.com" \
  --update-env-vars="COORDINATOR_ADDR=34.94.62.66:50051" \
  --cpu=4 \
  --memory=8Gi \
  --task-timeout=3600 \
  --quiet

gcloud run jobs update loom-worker \
  --image="${AR_URI}/loom-worker:latest" \
  --region="${REGION}" \
  --service-account="skewer-worker@${PROJECT_ID}.iam.gserviceaccount.com" \
  --update-env-vars="COORDINATOR_ADDR=34.94.62.66:50051" \
  --cpu=2 \
  --memory=4Gi \
  --task-timeout=1800 \
  --quiet

# 3. Deploy Coordinator to GKE
echo "[DEPLOY] Deploying coordinator to GKE..."
kubectl apply -f deployments/k8s/coordinator-cloud.yaml
kubectl rollout restart deployment/skewer-coordinator

echo "[DEPLOY] Waiting for coordinator to be ready..."
kubectl rollout status deployment/skewer-coordinator --timeout=120s

echo "[DEPLOY] Success! Cloud environment is ready."
echo "[DEPLOY] You can now submit jobs using: skewer-cli submit --scene gs://${BUCKET}/scenes/cornell.json --output gs://${BUCKET}/renders/test/"
