# Building

This guide covers building Skewer from source. For dependencies, see [Installation](installation.md).

## Prerequisites

Ensure you have the following installed:
- **C++20 Compiler** (Clang 17 recommended for full C++20 feature support and best performance)
- **CMake 3.21+**
- **Go 1.22+** (for orchestration tools)
- **OpenEXR, Imath, Zlib, libpng**

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

| Preset | Description |
|--------|-------------|
| `relwithdebinfo` | **Default** - Release build with debug symbols (recommended) |
| `debug` | Debug build with symbols, no optimizations |
| `release` | Optimized release build (-O3) |
| `ci` | CI build with tests enabled |
| `tidy` | Build with clang-tidy analysis |

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

## Dependencies

### vcpkg (Recommended)

The project uses vcpkg for dependency management. A `vcpkg.json` manifest is included:

```bash
# Install vcpkg if not already installed
git clone https://github.com/microsoft/vcpkg.git /path/to/vcpkg
cd vcpkg
./bootstrap-vcpkg.sh

# Integration (one-time)
./vcpkg integrate install
```

CMake will automatically find dependencies via vcpkg when using the toolchain file:

```bash
cmake --preset relwithdebinfo -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### System Dependencies

If not using vcpkg, install dependencies manually:

**Ubuntu:**
```bash
sudo apt-get update
sudo apt-get install -y \
    libopenexr-dev libimath-dev zlib1g-dev libpng-dev \
    libgtest-dev libgmock-dev
```

**macOS (Homebrew):**
```bash
brew install openexr libpng googletest
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

## Protobuf Generation

When modifying `.proto` files, regenerate the code for both Go and C++.

### Go Protobuf Code

```bash
# From project root
bash scripts/gen_proto.sh
```

This regenerates:
- `api/proto/coordinator/v1/coordinator.pb.go`
- `api/proto/coordinator/v1/coordinator_grpc.pb.go`

### C++ Protobuf Code

C++ protobuf files are generated automatically by CMake during the build. To regenerate manually:

```bash
# Using protoc directly
protoc \
    --cpp_out=api/proto/coordinator/v1/ \
    --grpc_out=api/proto/coordinator/v1/ \
    --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
    -I api/proto \
    api/proto/coordinator/v1/coordinator.proto
```

Or simply rebuild after modifying proto files:

```bash
cmake --build --preset relwithdebinfo --parallel
```

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

### Clean rebuild

To perform a clean rebuild:

```bash
rm -rf build/
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo --parallel
```

## Code Formatting

Format C++ code before committing:

```bash
./scripts/format.sh
```

This uses clang-format (installed locally via pip) to ensure consistent code style.
