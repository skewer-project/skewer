# Installation

> **Note:** These instructions are for building from source.
> Eventually, pre-built binaries will be available on package managers on different platforms.

## Clone & Setup

```bash
# Clone the repository
git clone https://github.com/skewer-project/skewer.git
cd skewer

# Pull large files tracked by Git LFS (required for sRGB spectral data)
git lfs install  # only needed once
git lfs pull
```

## Prerequisites

- CMake 3.21+
- C++20 compiler (C++17 minimum supported, C++20 recommended)
- Go 1.21+ (for CLI and coordinator)
- Protocol Buffer compiler (`protoc`)
- Go protobuf plugin: `go install google.golang.org/protobuf/cmd/protoc-gen-go@latest`
- gRPC Go plugin: `go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest`
- Python 3 (for code formatting script)

## System Dependencies

### Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y libopenexr-dev libimath-dev zlib1g-dev libpng-dev protobuf-compiler libgrpc++-dev
```

### macOS (Homebrew)

```bash
brew install openexr libpng protobuf grpc
```

### Additional Tools

For local deployment:

- **Docker CLI** - Building container images
- **Kubernetes** - [OrbStack](https://orbstack.dev/) (recommended for macOS) or [Minikube](https://minikube.sigs.k8s.io/)
- **kubectl** - Kubernetes CLI

## Verify Installation

```bash
# Check C++ compiler
clang++ --version  # or g++ --version

# Check CMake
cmake --version

# Check Go
go version

# Check protobuf
protoc --version

# Check Python (for formatting script)
python3 --version
```

## vcpkg (Recommended)

For dependency management, the project uses vcpkg:

```bash
# Install vcpkg (one-time)
git clone https://github.com/microsoft/vcpkg.git /path/to/vcpkg
cd /path/to/vcpkg
./bootstrap-vcpkg.sh

# Integrate with CMake (one-time)
/path/to/vcpkg integrate install
```

When building, use the vcpkg toolchain:

```bash
cmake --preset relwithdebinfo -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
```
