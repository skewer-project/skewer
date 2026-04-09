# Skewer & Loom

This repo contains the following projects:
- `skewer` - A ray-tracing deep renderer
- `loom` - A deep compositor
- `libs/exrio` - shared deep EXR helpers

## Scene conversion (`tools/`)

Python helpers for working with Blender and Skewer JSON:

- [`tools/blender_to_skewer/`](tools/blender_to_skewer/) — run `blender_export.py` inside Blender to export to Skewer scene JSON.
- [`tools/skewer_to_blend/`](tools/skewer_to_blend/) — run `convert.py` with Blender’s CLI to build a `.blend` from a Skewer JSON scene.

## Prerequisites

- CMake 3.21+
- C++17 compiler
- OpenEXR + Imath
- Zlib
- libpng (recommended, used by `loom` when available)

### Ubuntu
```bash
sudo apt-get update
sudo apt-get install -y libopenexr-dev libimath-dev zlib1g-dev libpng-dev
```

### macOS (Homebrew)
```bash
brew install openexr libpng
```

## Build

Use CMake presets from the repo root:

```bash
cmake --list-presets
cmake --preset release
cmake --build --preset release --parallel
```

## Test
```bash
cmake --preset ci (or release)
cmake --build --preset ci --parallel
ctest --preset ci
```

## Deployment & CLI

The distributed worker cluster manages rendering and compositing workloads. Control the cluster via the `skewer-cli` interface:
- **`render`**: Submit standard `.json` scenes to the compute cluster.
- **`composite`**: Deep merge layered EXRs using the Loom pipeline.

For automated local deployment scripts (OrbStack/Minikube) and the exhaustive CLI manual, please refer directly to the [Local Deployment & CLI Guide](LOCAL_DEPLOYMENT.md).
