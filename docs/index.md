# Skewer & Loom

Skewer is a distributed deep rendering system, consisting of:

- **Skewer** - A ray-tracing deep renderer
- **Loom** - A deep compositor
- **libs/exrio** - Shared deep EXR helpers

## Quick Links

- [Installation](getting-started/installation.md)
- [Quick Start](getting-started/quick-start.md)
- [CLI Reference](usage/cli.md)
- [Architecture](architecture/overview.md)
- [Deployment](deployment/local.md)

## Key Features

- **Deep EXR Rendering** - Per-pixel opacity as a linear-piecewise function of depth
- **Distributed Rendering** - Scale across multiple machines via GKE
- **Deep Compositing** - Layer-based compositing with accurate transparency handling
- **Scene Previewer** - Web-based preview tool for scene visualization

## Project Structure

| Directory | Description |
|-----------|-------------|
| `skewer/` | C++ ray-tracing renderer |
| `loom/` | C++ deep compositor |
| `libs/` | Shared C++ libraries (exrio) |
| `orchestration/` | Go CLI and coordinator |
| `apps/scene-previewer/` | React-based scene previewer web app |
| `api/` | Protocol Buffer definitions |
| `scripts/` | Utility scripts (formatting, proto generation) |
| `deployments/` | Docker and Kubernetes configs |
| `docs/` | Documentation |
