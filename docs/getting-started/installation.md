# Installation

## Prerequisites

- CMake 3.21+
- C++17 compiler
- Go 1.21+ (for CLI and coordinator)
- Protocol Buffer compiler (`protoc`)
- Go protobuf plugin: `go install google.golang.org/protobuf/cmd/protoc-gen-go@latest`
- gRPC Go plugin: `go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest`

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
```
