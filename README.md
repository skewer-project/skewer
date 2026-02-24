# Skewer & Loom

This repo contains the following projects:
- `skewer` - A ray-tracing deep renderer
- `loom` - A deep compositor
- `libs/exrio` - shared deep EXR helpers

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
