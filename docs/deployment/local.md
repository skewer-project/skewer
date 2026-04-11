# Local Deployment

Deploy Skewer locally using OrbStack or Minikube for development and testing.

## Prerequisites

- Docker CLI
- Kubernetes (OrbStack or Minikube)
- kubectl
- Go 1.21+

## Starting the Cluster

The deployment is automated:

```bash
./deployments/deploy_local.sh --rebuild
```

**Flags:**

| Flag | Description | Default |
|------|-------------|---------|
| `--rebuild` | Rebuild Docker images and CLI | false |
| `--workers` | Max worker count | 4 |
| `--runtime` | `orbstack` or `minikube` | orbstack |

## What Happens

1. Builds Docker images for:
   - Coordinator
   - Skewer worker
   - Loom worker
2. Starts K8s cluster (if not running)
3. Deploys manifests from `deployments/k8s/`
4. Starts port-forward to coordinator

## Stopping the Cluster

```bash
./deployments/stop_local.sh
```

This stops all pods and cleans up resources. Your local files (scenes, renders) are preserved.

## Verifying Deployment

```bash
# Check pods
kubectl get pods

# Check coordinator logs
kubectl logs -l app=coordinator

# Check worker logs
kubectl logs -l app=skewer-worker
```

## Accessing Services

### Coordinator

The coordinator is available at `localhost:50051`. The CLI handles port-forward automatically.

### CLI Usage

```bash
# Submit job
./orchestration/cmd/cli/skewer-cli submit --scene data/scenes/panda-####.json --frames 4 --output data/renders/test/

# Check status
./orchestration/cmd/cli/skewer-cli status --job <JOB_ID>
```

## Data Mapping

The repo root is mounted to `/data` in containers:

| Host Path | Container Path |
|-----------|----------------|
| `data/scenes/` | `/data/scenes/` |
| `data/renders/` | `/data/renders/` |

You can use either path style in commands - the coordinator rewrites paths automatically.

## Troubleshooting

### Pods not starting

```bash
kubectl describe pod <pod-name>
```

### Logs

```bash
kubectl logs <pod-name>
```

### Restart deployment

```bash
kubectl rollout restart deployment/coordinator
```
