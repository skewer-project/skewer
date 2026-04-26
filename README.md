# Skewer & Loom

This repo contains a high-performance, serverless distributed rendering system optimized for Google Cloud Platform.

- **`skewer`**: A C++ ray-tracing deep renderer.
- **`loom`**: A C++ deep compositor for merging layers with accurate transparency.
- **`orchestration`**: Go-based CLI and Cloud Run Coordinator.
- **`libs/exrio`**: Shared deep EXR C++ helpers.

## Scene Conversion

Python helpers for working with Blender and Skewer JSON are located in [`scripts/blender/`](scripts/blender/).

## Prerequisites

- **CMake 3.21+**
- **C++17 compiler** (Clang 17 recommended)
- **OpenEXR + Imath**
- **Zlib**
- **libpng**

### Ubuntu
```bash
sudo apt-get update
sudo apt-get install -y libopenexr-dev libimath-dev zlib1g-dev libpng-dev libgrpc++-dev protobuf-compiler-grpc
```

### macOS (Homebrew)
```bash
brew install openexr libpng grpc
```

## Build

Use CMake presets from the repo root:

```bash
# Configure
cmake --preset release

# Build everything
cmake --build --preset release --parallel

# Build specific worker
cmake --build --preset release --target skewer-worker
```

## Documentation

Comprehensive documentation is available in the [`docs/`](docs/) directory:

- **[Architecture Overview](docs/architecture/overview.md)**: How the serverless pipeline works.
- **[GCP Deployment Guide](docs/deployment/gcp.md)**: Deploying to production using Terraform.
- **[Local Development](docs/deployment/local.md)**: Setting up a local environment.
- **[CLI Reference](docs/usage/cli.md)**: Submitting and managing jobs.

## Deployment

The system is designed to run on GCP using **Cloud Run**, **Cloud Workflows**, and **Cloud Batch**. Infrastructure is managed via **Terraform** in `deployments/terraform/`. CI/CD is handled by **Cloud Build** as defined in `deployments/cloudbuild.yaml`.
