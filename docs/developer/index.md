# Developer Overview & Onboarding

Welcome to the Skewer & Loom developer documentation. This guide is designed for engineers looking to contribute to the core rendering and compositing engines.

## Codebase Map

The project is a mono-repo containing several distinct components:

- **`skewer/`**: The core C++ ray-tracing renderer.
- **`loom/`**: The C++ deep compositing engine.
- **`orchestration/`**: Go-based tools for CLI and cloud coordination.
- **`api/`**: Protobuf definitions for cross-component communication.
- **`libs/`**: Internal libraries shared across components (e.g., `exrio`).

## How to Navigate This Guide

The Developer Guide is structured in two tiers:

- **System Architecture**: High-level conceptual overviews of our unique systems (The Deep Image System, Path Tracer, etc.). Start here to understand the "Mental Model."
- **Module Reference**: Low-level documentation mapped directly to the `src/` directory. Use this when you are working in a specific part of the code and need technical details.

## Local Setup & Standards

### Build Presets
Refer to the [Building Guide](../getting-started/building.md) for full instructions. For core development, use the `relwithdebinfo` preset to maintain performance while keeping symbols for debugging:

```bash
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo
```

### Coding Standards
- **Google C++ Style**: We strictly follow the Google C++ Style Guide.
- **Formatting**: Run `./scripts/format.sh` before every commit.
- **Modern C++**: We use C++20 features (e.g., `std::atomic`, `std::shared_mutex`).
- **No Virtuals in Hot Loops**: To maximize cache performance, we avoid virtual function calls inside the path tracing kernels. Prefer bit-packed dispatch or switch-based orchestration.
