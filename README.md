# Skewer & Loom

A high-performance, serverless distributed rendering system for Google Cloud Platform.

- **`skewer/`** — C++ ray-tracing deep renderer
- **`loom/`** — C++ deep compositor
- **`orchestration/`** — Go CLI and Cloud Run Coordinator
- **`libs/exrio/`** — Shared deep EXR helpers

## Documentation

| Guide | Description |
|---|---|
| [Installation](docs/getting-started/installation.md) | Dependencies and setup |
| [Building](docs/getting-started/building.md) | Compile from source with CMake |
| [Quick Start](docs/getting-started/quick-start.md) | Render your first scene |
| [Local Development](docs/getting-started/local.md) | Run and test locally |
| [GCP Deployment](docs/getting-started/gcp.md) | Serverless cloud render farm |
| [Scene Format](docs/reference/scene-format.md) | Scene JSON specification |
| [CLI Reference](docs/reference/cli.md) | All command-line options |
| [Rendering Tips](docs/reference/rendering-tips.md) | Quality and performance |
| [Animation](docs/reference/animation.md) | Keyframe animation and motion blur |
| [Mathematical Foundations](docs/reference/math.md) | Rendering math and physics |
| [Previewer](docs/reference/previewer.md) | Web-based scene editor |
| [Architecture Overview](docs/developer/overview.md) | System design and data flow |
| [Skewer Renderer](docs/developer/skewer/architecture.md) | Ray tracer internals |
| [Loom Compositor](docs/developer/loom/index.md) | Deep compositing algorithm |
| [API & Coordinator](docs/developer/api/coordinator.md) | HTTP and gRPC APIs |
| [GKE Deployment (legacy)](docs/legacy/gke.md) | Deprecated Kubernetes setup |

## Scene Conversion

Python helpers for Blender ↔ Skewer conversion are in [`scripts/blender/`](scripts/blender/).

## Quick Start

1. Set up dependencies per [Installation](docs/getting-started/installation.md)
2. Build with CMake: `cmake --preset relwithdebinfo && cmake --build --preset relwithdebinfo --parallel`
3. Or follow the [GCP Deployment Guide](docs/getting-started/gcp.md) for cloud rendering
4. Open the [Scene Previewer](apps/scene-previewer/) and render your first scene

## Infrastructure

Infrastructure is managed via **Terraform** in [`deployments/terraform/`](deployments/terraform/). CI/CD is handled by **Cloud Build** via [`deployments/cloudbuild.yaml`](deployments/cloudbuild.yaml).
