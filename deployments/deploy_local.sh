#!/usr/bin/env bash

# Define what should happen on cancel
cleanup() {
    echo -e "\n[!] Deployment interrupted! Cleaning up..."
    
    # Revert the docker environment back to your Mac's host daemon
    eval $(minikube docker-env -u) 2>/dev/null || true
    echo "Restored Docker daemon to host machine."
    
    exit 1
}

# Trap Ctrl+C and run cleanup
trap cleanup INT TERM

# Exit on error
set -e

# Move to the project root directory so docker builds context correctly
cd "$(dirname "$0")/.."

# Check if minikube is running and start it if it isn't
MINIKUBE_ACTIVE=$(minikube status | grep -c "Running")
if [ "$MINIKUBE_ACTIVE" -eq 0 ]; then 
    echo "Minikube is not running. Starting minikube..."
    minikube start
else
    echo "Minikube is already running."
fi

# Use host docker daemon (DO NOT eval minikube docker-env here for building)
# This uses the host machine's RAM/CPU which is much faster/stable for C++ builds.

# Build the images locally
docker build -t skewer-worker:latest -f deployments/docker/Dockerfile.skewer .
# docker build -t loom-worker:latest -f deployments/docker/Dockerfile.loom .
docker build -t skewer-coordinator:latest -f deployments/docker/Dockerfile.coordinator .

# Load the images into minikube
echo "Loading images into minikube..."
minikube image load skewer-worker:latest
# minikube image load loom-worker:latest
minikube image load skewer-coordinator:latest

# Push the images to the registry (TODO: connect to GKE registry)
# docker push skewer-worker:latest
# docker push loom-worker:latest
# docker push skewer-coordinator:latest

# Apply the coordinator, skewer-worker, and loom-worker deployments
kubectl apply -f deployments/k8s/coordinator.yaml
kubectl apply -f deployments/k8s/skewer-worker.yaml
kubectl apply -f deployments/k8s/loom-worker.yaml

# Wait for the coordinator to be ready
kubectl wait --for=condition=ready pod -l app=skewer-coordinator --timeout=120s
kubectl get pods # print the pods

echo "Skewer cluster is ready!"
