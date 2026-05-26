# Pre-built Binaries

Pre-built binaries of `skewer-render` (the path-tracing renderer) and `loom` (the deep compositor)
are published for every tagged release on the
[Releases page](https://github.com/skewer-project/skewer/releases).

## Supported Platforms

| Platform              | Archive name                                  |
| --------------------- | --------------------------------------------- |
| Linux x86_64          | `skewer-{version}-linux-x86_64.tar.gz`        |
| macOS ARM64 (Apple Silicon) | `skewer-{version}-macos-arm64.tar.gz`   |
| macOS x86_64 (Intel)  | `skewer-{version}-macos-x86_64.tar.gz`        |
| Windows x64           | `skewer-{version}-windows-x86_64.zip`         |

## Download

Browse the [Releases page](https://github.com/skewer-project/skewer/releases) to find the latest version and your platform.

### From the Command Line

```bash
# Replace TAG with the release tag (e.g. v3.0.0) and PLATFORM with your platform
TAG="v3.0.0"
PLATFORM="linux-x86_64"   # or macos-arm64, macos-x86_64, windows-x86_64

curl -LO "https://github.com/skewer-project/skewer/releases/download/${TAG}/skewer-${TAG}-${PLATFORM}.tar.gz"
```

For Windows, replace `.tar.gz` with `.zip`.

### Verify Checksums

Every archive has a matching `.sha256` file. Verify the download:

```bash
curl -LO "https://github.com/skewer-project/skewer/releases/download/${TAG}/skewer-${TAG}-${PLATFORM}.tar.gz.sha256"
sha256sum -c skewer-${TAG}-${PLATFORM}.tar.gz.sha256
```

## Extract and Run

### Linux / macOS

```bash
# Extract
tar xzf skewer-${TAG}-${PLATFORM}.tar.gz

# The binaries are extracted into the current directory
./skewer-render scene.json
```

On macOS, you may need to allow the binary to run the first time (Gatekeeper):

```bash
xattr -d com.apple.quarantine ./skewer-render
xattr -d com.apple.quarantine ./loom
```

### Windows

Extract the `.zip` archive, then open a Command Prompt or PowerShell in the extracted folder:

```cmd
skewer-render.exe scene.json
```

!!! warning "CPU requirement"
    The pre-built Windows binary is compiled with `/arch:AVX2`. It requires a CPU with
    AVX2 support (Intel Haswell 2013+, AMD Excavator 2015+). If you have an older
    processor, [build from source](building.md) instead.

## What's Included

Each archive contains:

| Binary           | Description                              |
| ---------------- | ---------------------------------------- |
| `skewer-render`  | The path-tracing renderer CLI            |
| `loom`           | The deep compositor CLI                  |
| Bundled shared libraries | Runtime dependencies (self-contained) |

The archives are **self-contained** — all required runtime libraries (system and third-party) are bundled alongside the binaries. No additional dependencies need to be installed.

## Next Steps

- [Quick Start](quick-start.md) — Render your first scene
- [Scene Format](../reference/scene-format.md) — Understanding scene JSON
- [Rendering Tips](../reference/rendering-tips.md) — Best practices for quality and performance
- [Building from Source](building.md) — If you need to build for development
