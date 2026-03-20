#!/usr/bin/env bash

echo "Stopping Skewer local cluster..."

# Move to the project root directory
cd "$(dirname "$0")/.." || exit 1

# Function to safely delete resources if they exist
delete_resources() {
    local file=$1
    if [ -f "$file" ]; then
        echo "Cleaning up resources from $file..."
        # We pipe through sed to handle the same placeholder logic as the deploy script
        # though for deletion, the exact path doesn't usually matter for the selector
        sed "s|{{SKEWER_DATA_PATH}}|$(pwd)|g" "$file" | kubectl delete --ignore-not-found -f -
    fi
}

# Delete resources in reverse order of deployment
delete_resources "deployments/k8s/skewer-worker.yaml"
delete_resources "deployments/k8s/loom-worker.yaml"
delete_resources "deployments/k8s/coordinator.yaml"

echo "-----------------------------------"
echo "All Kubernetes resources removed."
echo "Note: Your local repository tree (scenes, renders, etc.) remains untouched."
