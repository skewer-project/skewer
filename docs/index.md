# Skewer & Loom

Skewer is a serverless, distributed deep rendering system designed for high-throughput, physically-based rendering on Google Cloud Platform.

## Quick Links

- [Installation](getting-started/installation.md)
- [Quick Start](getting-started/quick-start.md)
- [Scene Format](usage/scene-format.md)
- [CLI Reference](usage/cli.md)
- [Rendering Tips](usage/rendering-tips.md)
- [Animation](usage/animation.md)
- [Compositing](usage/compositing.md)
- [Previewer](usage/previewer.md)
- [Architecture Overview](architecture/overview.md)
- [Mathematical Foundations](architecture/math.md)
- [GCP Deployment (Production)](deployment/gcp.md)
- [Local Deployment (Development)](deployment/local.md)

## Key Features

- **Deep EXR Rendering** - Per-pixel opacity as a linear-piecewise function of depth.
- **Serverless Architecture** - Automated scaling via Cloud Run, Cloud Workflows, and Cloud Batch.
- **Deep Compositing** - Correct transparency handling for overlapping volumes.
- **Cost-Optimized** - Native support for Spot VMs and Scale-to-Zero managed services.
- **Scene Previewer** - Web-based preview tool for scene visualization.

## Project Structure

| Directory | Description |
|-----------|-------------|
| `skewer/` | C++ ray-tracing renderer. |
| `loom/` | C++ deep compositor. |
| `libs/` | Shared C++ libraries (exrio). |
| `orchestration/` | Go CLI and serverless Coordinator. |
| `deployments/` | Terraform, Cloud Build, and Docker configurations. |
| `apps/scene-previewer/` | React-based scene previewer web app. |
| `api/` | Protocol Buffer definitions. |
| `docs/` | Comprehensive system documentation. |
| `scripts/` | Utility scripts (formatting, proto generation, stitching). |
