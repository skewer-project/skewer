# CLI Reference

Reference for the three user-facing binaries: `skewer-cli`, `skewer-render`, and `loom`.

## skewer-cli

The `skewer-cli` submits rendering jobs to the coordinator, checks their status, and cancels them.

### submit

```bash
./orchestration/cmd/cli/skewer-cli submit [flags]
```

**Flags:**

| Flag | Description |
|------|-------------|
| `-s, --scene` | URI of the scene file (local or `gs://`) **[Required]** |
| `-f, --frames` | Number of frames to render |
| `-S, --samples` | Maximum samples per pixel (overrides JSON) |
| `-W, --width` | Image width (overrides JSON) |
| `-H, --height` | Image height (overrides JSON) |
| `--deep` | Enable deep EXR output |
| `-o, --output` | Output directory for renders |

**Basic Usage:**

```bash
./orchestration/cmd/cli/skewer-cli submit \
  --scene data/scenes/panda-####.json \
  --frames 4 \
  --output data/renders/my_job/
```

**With Overrides:**

```bash
./orchestration/cmd/cli/skewer-cli submit \
  --scene data/scenes/panda-####.json \
  --frames 4 \
  --samples 256 \
  --width 1920 \
  --height 1080 \
  --deep \
  --output data/renders/my_job/
```

**Frame Sequences:** Use `####` in the filename to render sequences (e.g., `panda-0001.json`, `panda-0002.json`).

---

### status

```bash
./orchestration/cmd/cli/skewer-cli status --job <JOB_ID>
```

**Example Output:**

```
[CLI] Coordinator not reachable. Attempting to port-forward...
[CLI]: Port-forward successful.
Job Status: JOB_STATUS_RUNNING
Progress: 25.0%
```

---

### cancel

```bash
./orchestration/cmd/cli/skewer-cli cancel --job <JOB_ID>
```

Drops all pending tasks and prevents further processing.

---

## skewer-render

The `skewer-render` binary runs the C++ ray tracer locally for rendering scenes defined in JSON.

!!! warning "Thread count"
    By default the renderer uses **all CPU cores**.
    If you're running other applications on your system, set the `threads` parameter in your
    scene JSON or use the `num_threads` argument to a number less than the available CPU cores available.
    See the [Rendering Tips](rendering-tips.md#thread-count) for details.

```bash
./build/relwithdebinfo/skewer/skewer-render <scene.json> [num_threads] [options]
```

**Arguments:**

| Argument | Description |
|----------|-------------|
| `scene.json` | Path to a JSON scene configuration file **[Required]** |
| `num_threads` | Override the number of render threads (optional) |

**Options:**

| Flag | Description |
|------|-------------|
| `--frame N` | Render only animated layers at frame index N |
| `--frames A..B` | Render animated layers for the inclusive frame range [A, B] |
| `--statics-only` | Render only non-animated layers (one output per layer) |
| `--help, -h` | Show this help message |

**Examples:**

```bash
# Render all static layers
./build/relwithdebinfo/skewer/skewer-render scenes/my_scene.json

# Render with 8 threads
./build/relwithdebinfo/skewer/skewer-render scenes/my_scene.json 8

# Render only frame 42 of animated layers
./build/relwithdebinfo/skewer/skewer-render scenes/my_scene.json --frame 42

# Render frames 0 through 99
./build/relwithdebinfo/skewer/skewer-render scenes/my_scene.json --frames 0..99
```

Output filenames are derived from each layer filename (e.g. `layer_character.json` produces `layer_character.png` and `layer_character.exr`). Configure output directory and image settings in the scene JSON.

---

## loom

The `loom` binary composites multiple deep EXR layers into a single flattened image.

```bash
./build/relwithdebinfo/loom/loom [options] <input1.exr> [input2.exr ...] <output_prefix>
```

**Options:**

| Flag | Description |
|------|-------------|
| `--deep-output` | Write merged deep EXR (default: off) |
| `--flat-output` | Write flattened EXR (default: on) |
| `--no-flat-output` | Don't write flattened EXR |
| `--png-output` | Write PNG preview (default: on) |
| `--no-png-output` | Don't write PNG preview |
| `--verbose, -v` | Detailed logging |
| `--merge-threshold N` | Depth epsilon for merging samples (default: 0.001) |
| `--help, -h` | Show this help message |

**Outputs:**

| File | Description |
|------|-------------|
| `<prefix>_merged.exr` | Deep EXR with all layers combined (if `--deep-output`) |
| `<prefix>_flat.exr` | Standard single-layer EXR |
| `<prefix>.png` | Preview image |

**Example:**

```bash
./build/relwithdebinfo/loom/loom --deep-output --verbose \
  renders/layer1/frame-0001.exr \
  renders/layer2/frame-0001.exr \
  composited/frame-0001
```

---

## CMake Presets

The project defines these configure presets in `CMakePresets.json`. Use them with `cmake --preset <name>`:

| Preset | Description |
|--------|-------------|
| `debug` | Debug build with symbols, no optimizations |
| `release` | Optimized release build (`-O3`) |
| `release-milan` | Release with native optimizations for AMD EPYC Milan (GCP N2D) |
| `release-portable` | Release build for bundled cross-platform binary archives |
| `relwithdebinfo` | **Default** — Release build with debug symbols |
| `ci` | CI-optimized build (no native optimizations) |
| `asan` | Debug build with AddressSanitizer + UndefinedBehaviorSanitizer |

Build with a preset:

```bash
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo --parallel 4 # build with up to four threads
```

Test with a preset:

```bash
ctest --preset relwithdebinfo
```

---

## See Also

- [Scene Format](scene-format.md) - Complete scene file specification
- [Rendering Tips](rendering-tips.md) - Best practices for quality and performance
- [GCP Deployment](../getting-started/gcp.md) - Cloud rendering setup
- [Local Development](../getting-started/local.md) - Local cluster setup
- [Architecture Overview](../developer/overview.md) - System design and data flow
- [Skewer Renderer](../developer/skewer/architecture.md) - Ray tracer internals
- [Loom Compositor](../developer/loom/index.md) - Deep compositing algorithm
