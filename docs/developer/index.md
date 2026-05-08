# Developer Overview & Onboarding

Welcome to the Skewer & Loom developer documentation. This guide is designed for engineers looking to contribute to the core rendering and compositing engines.

## Codebase Map

The project is divided into several key areas:

- **`skewer/`**: The core C++ ray-tracing renderer.
- **`loom/`**: The C++ deep compositing engine.
- **`orchestration/`**: Go-based tools for CLI and cloud coordination.
- **`api/`**: Protobuf definitions for cross-component communication.
- **`libs/`**: Internal libraries shared across components (e.g., `exrio`).

## Local Setup

Refer to the [Building Guide](../getting-started/building.md) for full dependency and build instructions. For core engine development, we recommend using the `relwithdebinfo` CMake preset.

```bash
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo
```
