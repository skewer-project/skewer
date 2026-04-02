#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

PROJECT=$1

if [ -z "$PROJECT" ]; then
    echo "[ERROR] You must provide your Google Cloud Project ID."
    echo "Usage: ./deployments/deploy_cloud.sh <your-gcp-project-id>"
    exit 1
fi

# Move to the project root directory
cd "$(dirname "$0")/.."

REGISTRY="gcr.io/$PROJECT"
echo "[CLI]: Targeting Google Container Registry: $REGISTRY"

echo "[CLI]: 1. Authenticating local Docker to gcloud..."
gcloud auth configure-docker --quiet

echo "\n[CLI]: 2. Building Docker Images (this might take a few minutes)..."
docker build -t ${REGISTRY}/skewer-coordinator:latest -f deployments/docker/Dockerfile.coordinator .
docker build -t ${REGISTRY}/skewer-worker:latest -f deployments/docker/Dockerfile.skewer .
docker build -t ${REGISTRY}/loom-worker:latest -f deployments/docker/Dockerfile.loom .

echo "\n[CLI]: 3. Pushing Docker Images to cloud..."
docker push ${REGISTRY}/skewer-coordinator:latest
docker push ${REGISTRY}/skewer-worker:latest
docker push ${REGISTRY}/loom-worker:latest

echo "\n[CLI]: 4. Applying Kubernetes manifests with updated Image URIs..."
# Cloud mode doesn't need to mount a physical local data directory, so we pass a dummy path.
DUMMY_PATH="/tmp/cloud-mount"

# Inject the GCR registry path and dynamic variables into the Kubernetes Deployments
sed -e "s|image: skewer-coordinator:latest|image: ${REGISTRY}/skewer-coordinator:latest|g" \
    -e "s|{{SKEWER_DATA_PATH}}|${DUMMY_PATH}|g" \
    -e "s|{{MAX_WORKERS}}|20|g" \
    deployments/k8s/coordinator.yaml | kubectl apply -f -

sed -e "s|image: skewer-worker:latest|image: ${REGISTRY}/skewer-worker:latest|g" \
    -e "s|{{SKEWER_DATA_PATH}}|${DUMMY_PATH}|g" \
    deployments/k8s/skewer-worker.yaml | kubectl apply -f -

sed -e "s|image: loom-worker:latest|image: ${REGISTRY}/loom-worker:latest|g" \
    -e "s|{{SKEWER_DATA_PATH}}|${DUMMY_PATH}|g" \
    deployments/k8s/loom-worker.yaml | kubectl apply -f -

echo "\n[CLI]: Waiting for Skewer Coordinator to boot..."
kubectl wait --for=condition=ready pod -l app=skewer-coordinator --timeout=120s

echo "\n[CLI]: Cloud deployment is complete!"
echo "       You can now submit tasks using: ./skewer-cli render ..."
