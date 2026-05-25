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

## Scene Previewer

The **[hosted previewer](http://skewer.pages.dev)** lets anyone edit and save scenes without
any local setup. You only need the local dev server (`bun run dev`) if you're modifying the
previewer itself or connecting to your own cloud render farm.
See the **[Previewer Guide](../reference/previewer.md)** for details.

## Building Loom (Deep Compositor)

Loom is built alongside the renderer in the same CMake build:

```bash
cmake --build --preset relwithdebinfo --target loom --parallel
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
# Build with tests enabled and run the test suite
cmake --preset ci
cmake --build --preset ci --parallel
ctest --preset ci --parallel

# Run a quick test render (configure output in scene.json)
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
