# Skewer & Loom

Skewer is a serverless, distributed deep rendering system designed for high-throughput, physically-based rendering on Google Cloud Platform.

## Quick Links

- [Installation](getting-started/installation.md)
- [Quick Start](getting-started/quick-start.md)
- [Scene Format](reference/scene-format.md)
- [CLI Reference](reference/cli.md)
- [Rendering Tips](reference/rendering-tips.md)
- [Animation](reference/animation.md)
- [Previewer](reference/previewer.md)
- [Architecture Overview](developer/overview.md)
- [Mathematical Foundations](reference/math.md)
- [GCP Deployment (Production)](getting-started/gcp.md)
- [Local Development](getting-started/local.md)
- [Building](getting-started/building.md)
- [Loom Compositor](developer/loom/index.md)
- [Skewer Renderer](developer/skewer/architecture.md)
- [API & Coordinator](developer/api/coordinator.md)

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
