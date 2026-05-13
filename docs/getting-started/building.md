# Building

This guide covers building Skewer from source. See [Installation](installation.md) for dependency setup.

## Using CMake Presets

The project uses CMake presets for consistent builds:

```bash
# List available presets
cmake --list-presets

# Configure a preset
cmake --preset relwithdebinfo

# Build with a preset
cmake --build --preset relwithdebinfo --parallel
```

## Available Presets

| Preset           | Description                                                  |
| ---------------- | ------------------------------------------------------------ |
| `relwithdebinfo` | **Default** - Release build with debug symbols (recommended) |
| `debug`          | Debug build with symbols, no optimizations                   |
| `release`        | Optimized release build (-O3)                                |
| `ci`             | CI build with tests enabled                                  |

See the [full preset list](../reference/cli.md#cmake-presets) in the CLI Reference for all available presets, including `asan`, `release-milan`, and `releasestatic`.

## Build Outputs

After building, binaries are located in `build/<preset>/`:

```
build/relwithdebinfo/
├── skewer/
│   ├── skewer-render    # Main rendering CLI
│   └── skewer-worker    # Worker for distributed renders
├── loom/
│   ├── loom            # Deep compositor
│   └── loom-worker     # Worker for deep compositing
├── libs/exrio/         # EXR library
└── api/                # Compiled protobuf C++ files
```

## Running Tests

```bash
# Build and run tests
cmake --preset ci
cmake --build --preset ci --parallel
ctest --preset ci
```

## Building the Go CLI

The Go CLI and Coordinator are built separately from the C++ codebase:

```bash
# Ensure Go dependencies are installed
cd orchestration
go mod download

# Build CLI
go build -o skewer-cli ./cmd/cli/

# Build Coordinator
go build -o coordinator ./cmd/coordinator/
```

The built binaries will be in `orchestration/`.

## Troubleshooting

### CMake can't find dependencies

If CMake can't find dependencies, you may need to specify paths:

```bash
cmake --preset relwithdebinfo -DCMAKE_PREFIX_PATH=/path/to/libs
```

### Build errors on macOS

If you encounter Apple Silicon build issues, ensure you're using the correct architecture:

```bash
cmake --preset relwithdebinfo -DCMAKE_OSX_ARCHITECTURE=arm64
```

## See Also

- [Local Development](local.md) — Running and testing after building
- [Quick Start](quick-start.md) — Render your first scene
- [CLI Reference](../reference/cli.md) — Full command-line documentation

### Clean rebuild

To perform a clean rebuild:

```bash
rm -rf build/
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo --parallel
```


