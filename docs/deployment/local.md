# Local Development

This guide covers how to build, run, and test Skewer locally. Skewer no longer uses Kubernetes for local development — the production architecture is fully serverless on GCP.

## Prerequisites

- CMake 3.20+
- A C++ compiler with C++20 support (Clang 14+, GCC 12+)
- [Bun](https://bun.sh/docs/installation) — for the scene previewer
- [Go](https://go.dev/dl/) 1.21+ — for the CLI

See [Installation](../getting-started/installation.md) and [Building](../getting-started/building.md) for detailed setup instructions.

## Building the Renderer

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

The binary is at `build/relwithdebinfo/skewer/skewer-render`.

## Running a Local Render

The simplest way to render is with the built binary:

```bash
./build/relwithdebinfo/skewer/skewer-render \
  --scene apps/scene-previewer/public/templates/scene.json \
  --output output.png
```

| Flag | Description |
|------|-------------|
| `--scene` | Path to scene JSON file |
| `--output` | Output image path (PNG or EXR) |
| `--width` | Override image width |
| `--height` | Override image height |
| `--samples` | Override max samples per pixel |
| `--threads` | Number of render threads (0 = auto) |

For the full CLI reference, see [CLI Reference](../usage/cli.md).

## Using the Scene Previewer

The web-based previewer lets you visualize and edit scenes before rendering:

```bash
cd apps/scene-previewer
bun install
bun run dev
```

Open http://localhost:5173. Features include:

- Load and inspect scene files with all layers parsed
- Select and edit objects (spheres, quads, OBJ meshes)
- Edit material properties (albedo, roughness, IOR, emission)
- Delete objects and navigate the scene tree
- Create new scenes from templates

See the [Previewer Guide](../usage/previewer.md) for full documentation.

## Building Loom (Deep Compositor)

Loom is built alongside the renderer in the same CMake build:

```bash
cmake --build build -j$(nproc) --target loom-worker
```

The binary is at `build/relwithdebinfo/loom/loom-worker`. You can use it to composite deep EXR files from multiple local renders:

```bash
./build/relwithdebinfo/loom/loom-worker \
  --input renders/layer1/frame-0001.exr renders/layer2/frame-0001.exr \
  --output composited/frame-0001
```

See [Compositing](../usage/compositing.md) for the full compositing workflow.

## Testing Your Setup

```bash
# Run the test suite
ctest --test-dir build -j$(nproc)

# Run a quick test render with the normals integrator
./build/relwithdebinfo/skewer/skewer-render \
  --scene apps/scene-previewer/public/templates/scene.json \
  --output normals.png \
  --width 800 --height 450
```

## Next Steps

- [Scene Format](../usage/scene-format.md) — Learn how to write scene files
- [Rendering Tips](../usage/rendering-tips.md) — Best practices for quality and performance
- [Animation](../usage/animation.md) — Keyframe animation and motion blur
- [Compositing](../usage/compositing.md) — Layer compositing with loom
- [GCP Deployment](../deployment/gcp.md) — Set up the cloud render farm for distributed rendering
