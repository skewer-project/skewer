# Skewer & Loom

A high-performance, serverless distributed rendering system for Google Cloud Platform.

> **Full documentation:** [skewer-project.github.io/skewer](https://skewer-project.github.io/skewer/)

- **`skewer/`** — C++ ray-tracing deep renderer
- **`loom/`** — C++ deep compositor
- **`orchestration/`** — Go CLI and Cloud Run Coordinator
- **`libs/exrio/`** — Shared deep EXR helpers

## Quick Start

1. Set up dependencies — see the [Installation guide](https://skewer-project.github.io/skewer/getting-started/installation/)
2. Build with CMake: `cmake --preset relwithdebinfo && cmake --build --preset relwithdebinfo --parallel 4`
3. Or follow the [GCP Deployment guide](https://skewer-project.github.io/skewer/getting-started/gcp/) for cloud rendering
4. Open the [Scene Previewer](https://skewer.pages.dev) and render your first scene

## Key Docs

| Guide | Link |
|---|---|
| Quick Start | [skewer-project.github.io/skewer/getting-started/quick-start/](https://skewer-project.github.io/skewer/getting-started/quick-start/) |
| CLI Reference | [skewer-project.github.io/skewer/reference/cli/](https://skewer-project.github.io/skewer/reference/cli/) |
| Scene Format | [skewer-project.github.io/skewer/reference/scene-format/](https://skewer-project.github.io/skewer/reference/scene-format/) |
| Rendering Tips | [skewer-project.github.io/skewer/reference/rendering-tips/](https://skewer-project.github.io/skewer/reference/rendering-tips/) |
| Architecture Overview | [skewer-project.github.io/skewer/developer/overview/](https://skewer-project.github.io/skewer/developer/overview/) |
| Full docs index | [skewer-project.github.io/skewer](https://skewer-project.github.io/skewer) |

## Infrastructure

Infrastructure is managed via **Terraform** in [`deployments/terraform/`](deployments/terraform/). CI/CD is handled by **Cloud Build** via [`deployments/cloudbuild.yaml`](deployments/cloudbuild.yaml).

Scene conversion helpers (Blender ↔ Skewer) are in [`scripts/blender/`](scripts/blender/).
