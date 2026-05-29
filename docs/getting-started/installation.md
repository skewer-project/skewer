# Installation

## Option 1: Pre-built Binaries (Recommended)

The fastest way to get started. Download `skewer-render` and `loom` from the
[Releases page](https://github.com/skewer-project/skewer/releases).

Pre-built archives are available for **Linux** (x86_64), **macOS** (ARM64 / Intel),
and **Windows** (x64). They are self-contained — no dependencies to install.

See the **[Pre-built Binaries](release-binaries.md)** guide for full instructions.

## Option 2: Build from Source

If you need to modify the source, run tests, or develop, build from source.

### Prerequisites (all platforms)

- **CMake** 3.21+
- **Clang++ 17 (or higher)**
- **Git LFS** — pull only what's needed to build: `git lfs install && git lfs pull --include="skewer/src/core/spectral/srgb_spec_data.cc"`
- **Go** 1.25+ (for CLI and coordinator)
- **Protocol Buffer compiler** (`protoc`)
- **Go protobuf plugins:**
  ```bash
  go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
  go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest
  ```
- **Python 3** (for formatting and utility scripts)

### Clone & Setup

```bash
git clone https://github.com/skewer-project/skewer.git
cd skewer

# Pull large files tracked by Git LFS (required for sRGB spectral data)
git lfs install                    # only needed once
git lfs pull --include="skewer/src/core/spectral/srgb_spec_data.cc"
```

If you plan to run tests, also pull the test fixtures:

```bash
git lfs pull  # pulls everything (golden images, test assets)
```

### Platform Dependencies

Choose the section that matches your platform:

#### Linux (Ubuntu / Debian)

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake \
  clang-17 \
  libopenexr-dev \
  libimath-dev \
  zlib1g-dev \
  libpng-dev \
  protobuf-compiler \
  libgrpc++-dev \
  ninja-build
```

#### macOS (Homebrew)

```bash
brew install cmake ninja openexr libpng protobuf grpc
```

#### Windows

Install the base toolchain with **[winget](https://learn.microsoft.com/en-us/windows/package-manager/)** (built into Windows 10/11):

```powershell
winget install Kitware.CMake LLVM.LLVM Git.Git Ninja-build.Ninja
```

Then use **[vcpkg](https://vcpkg.io/)** for C++ libraries (the CMake presets are configured for it):

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg.exe install openexr libpng zlib protobuf grpc --triplet x64-windows
```

Set the environment variable so CMake can find vcpkg:

```powershell
[Environment]::SetEnvironmentVariable('VCPKG_ROOT', 'C:\vcpkg', 'User')
```

#### Cross-platform: conda

If you prefer a single package manager across all platforms,
**[conda](https://docs.conda.io/)** (via conda-forge) can install everything:

```bash
# Create and activate a conda environment
conda create -n skewer -c conda-forge \
  cmake ninja clang openexr libpng zlib protobuf grpc-cpp
conda activate skewer
```

Then configure CMake pointing at the conda environment:

```bash
cmake --preset relwithdebinfo \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX"
```

#### Cross-platform: vcpkg (standalone)

You can also use vcpkg on Linux and macOS if you prefer a unified C++ package manager
instead of system packages:

```bash
# Install vcpkg (one-time)
git clone https://github.com/microsoft/vcpkg.git /path/to/vcpkg
cd /path/to/vcpkg
./bootstrap-vcpkg.sh

# Set the environment variable
export VCPKG_ROOT=/path/to/vcpkg
```

When `VCPKG_ROOT` is set, pass the vcpkg toolchain to any CMake preset:

```bash
cmake --preset relwithdebinfo \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

See [Building](building.md) for all available presets.

### Verify Installation

```bash
# Check C++ compiler
clang++ --version   # or g++ --version

# Check CMake
cmake --version

# Check Go
go version

# Check protobuf
protoc --version

# Check Python
python3 --version
```

## Also see

- [Pre-built Binaries](release-binaries.md) — Download and run without building
- [Building](building.md) — Build Skewer from source
- [Quick Start](quick-start.md) — Render your first scene
- [Local Development](local.md) — Run and test locally
