# Local Development

This guide covers how to build, run, and test Skewer locally. Skewer no longer uses Kubernetes for local development — the production architecture is fully serverless on GCP.

If you just want to render scenes without building from source, use the **[Pre-built Binaries](release-binaries.md)** instead.

See [Installation](installation.md) for dependency setup and [Building](building.md) for full build instructions.

Once built, the renderer binary is at `build/relwithdebinfo/skewer/skewer-render`.

## Running a Local Render

```bash
./build/relwithdebinfo/skewer/skewer-render <scene.json> [num_threads]
```

| Argument | Description |
|----------|-------------|
| `scene.json` | Path to a JSON scene configuration file |
| `num_threads` | Override thread count (optional) |

**Flags:**

| Flag | Description |
|------|-------------|
| `--frame N` | Render only animated layers at frame index N |
| `--frames A..B` | Render animated layers for inclusive frame range [A, B] |
| `--statics-only` | Render only non-animated layers (one output per layer) |
| `--help, -h` | Show help message |

For the full CLI reference, see [CLI Reference](../reference/cli.md).

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

See the [Previewer Guide](../reference/previewer.md) for full documentation.

## Building Loom (Deep Compositor)

Loom is built alongside the renderer in the same CMake build:

```bash
cmake --build build -j$(nproc) --target loom
```

The binary is at `build/relwithdebinfo/loom/loom`. See the [CLI Reference](../reference/cli.md) for all flags and options.

**Example usage:**

```bash
./build/relwithdebinfo/loom/loom --deep-output --verbose \
  layer1/frame-0001.exr layer2/frame-0001.exr \
  composited/frame-0001
```

## Testing Your Setup

```bash
# Run the test suite
ctest --test-dir build -j$(nproc)

# Run a quick test render with the normals integrator (configure output in scene.json)
./build/relwithdebinfo/skewer/skewer-render \
  apps/scene-previewer/public/templates/scene.json
```

## Next Steps

- [Scene Format](../reference/scene-format.md) — Learn how to write scene files
- [Rendering Tips](../reference/rendering-tips.md) — Best practices for quality and performance
- [Animation](../reference/animation.md) — Keyframe animation and motion blur
- [GCP Deployment](gcp.md) — Set up the cloud render farm for distributed rendering
- [CLI Reference](../reference/cli.md) — Complete CLI documentation
- [Compositing](../developer/loom/index.md) — Layer compositing with loom
- [Architecture Overview](../developer/overview.md) — How the system works
