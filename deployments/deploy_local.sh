#!/usr/bin/env bash

# Default runtime
RUNTIME="orbstack"
REBUILD=false
WORKERS="4" # Default to 4 local workers

# Parse arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --runtime) RUNTIME="$2"; shift ;;
        --rebuild) REBUILD=true ;;
        --workers) WORKERS="$2"; shift ;;
        *) echo "Unknown parameter passed: $1"; exit 1 ;;
    esac
    shift
done

echo "Starting deployment using runtime: ${RUNTIME} with ${WORKERS} max workers."

# Check if images exist if rebuild is not forced
if [ "$REBUILD" = false ]; then
    if ! docker image inspect skewer-worker:latest >/dev/null 2>&1 || \
       ! docker image inspect loom-worker:latest >/dev/null 2>&1 || \
       ! docker image inspect skewer-coordinator:latest >/dev/null 2>&1; then
        echo "One or more images are missing. Forcing rebuild..."
        REBUILD=true
    fi
fi

# Define what should happen on cancel
cleanup() {
    echo -e "\n[!] Deployment interrupted! Cleaning up..."

    if [ "$RUNTIME" == "minikube" ]; then
        # Revert the docker environment back to your host daemon
        eval $(minikube docker-env -u) 2>/dev/null || true
        echo "Restored Docker daemon to host machine."
    fi

    exit 1
}

# Trap Ctrl+C and run cleanup
trap cleanup INT TERM

# Exit on error
set -e

# Move to the project root directory so docker builds context correctly
cd "$(dirname "$0")/.."

# Check cluster status and start if necessary
if [ "$RUNTIME" == "minikube" ]; then
    MINIKUBE_ACTIVE=$(minikube status | grep -c "Running") || true
    if [ "$MINIKUBE_ACTIVE" -eq 0 ]; then
        echo "Minikube is not running. Starting minikube..."
        minikube start
    else
        echo "Minikube is already running."
    fi
elif [ "$RUNTIME" == "orbstack" ]; then
    # OrbStack is usually always running or managed via its GUI.
    # We just check if kubectl can connect.
    if ! kubectl cluster-info &> /dev/null; then
        echo "[ERROR]: Cannot connect to Kubernetes cluster. Starting orbstack..."
        orb start
    fi
    echo "OrbStack cluster is active."
fi

if [ "$REBUILD" = true ]; then
    # Build the local CLI
    echo "Building local Skewer CLI..."
    go build -o apps/cli/skewer-cli apps/cli/main.go
    echo "CLI built at apps/cli/skewer-cli"

    # Build the images locally
    echo "Building Docker images..."
    docker build -t skewer-worker:latest -f deployments/docker/Dockerfile.skewer .
    docker build -t loom-worker:latest -f deployments/docker/Dockerfile.loom .
    docker build -t skewer-coordinator:latest -f deployments/docker/Dockerfile.coordinator .

    # Handle image availability in the cluster
    if [ "$RUNTIME" == "minikube" ]; then
        echo "Loading images into minikube..."
        minikube image load skewer-worker:latest
        minikube image load loom-worker:latest
        minikube image load skewer-coordinator:latest
    elif [ "$RUNTIME" == "orbstack" ]; then
        # OrbStack shares the local Docker engine with Kubernetes,
        # so images are typically available immediately after build.
        echo "Images are built and available for OrbStack."
    fi
else
    echo "Skipping image build (use --rebuild to force)."
fi

# Resolve the absolute path of the data directory for hostPath mounting
# Note: On Mac, /Users/ is shared with the VM by default in both Minikube and OrbStack.
SKEWER_DATA_PATH="$(pwd)/data"
echo "Mapping local data directory: ${SKEWER_DATA_PATH}"

# Apply the coordinator, skewer-worker, and loom-worker deployments with path injection
# We use sed to replace the placeholder with the absolute path
sed -e "s|{{SKEWER_DATA_PATH}}|${SKEWER_DATA_PATH}|g" -e "s|{{MAX_WORKERS}}|${WORKERS}|g" deployments/k8s/coordinator.yaml | kubectl apply -f -
sed "s|{{SKEWER_DATA_PATH}}|${SKEWER_DATA_PATH}|g" deployments/k8s/skewer-worker.yaml | kubectl apply -f -
sed "s|{{SKEWER_DATA_PATH}}|${SKEWER_DATA_PATH}|g" deployments/k8s/loom-worker.yaml | kubectl apply -f -

# Wait for the coordinator to be ready
echo "Waiting for coordinator to be ready..."
kubectl wait --for=condition=ready pod -l app=skewer-coordinator --timeout=120s
kubectl get pods

echo "Skewer cluster is ready!"
