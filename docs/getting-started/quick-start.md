# Quick Start

!!! abstract "Travis and TA Quick Start: Link"
    If you are here for interactive grading, jump to the
    **[Travis and TA Quick Start →](travis-ta-guide.md)**

This guide walks through your first render with Skewer. See [Installation](installation.md) and [Building](building.md) for detailed setup instructions.

## Prerequisites

Choose how to get the renderer:

- **[Pre-built Binaries](release-binaries.md)** — Download `skewer-render` and `loom` from the
  [Releases page](https://github.com/skewer-project/skewer/releases). No build tools needed.
- **[Build from Source](building.md)** — If you're developing or modifying the code.

## 1. Run a Local Render

Run the renderer binary directly on a scene file:

```bash
# Pre-built binary
./skewer-render <path-to-scene.json>

# Local build
./build/relwithdebinfo/skewer/skewer-render <path-to-scene.json>
```

!!! tip "Need a scene to render?"
    Use the **[Scene Previewer](https://skewer.pages.dev)** to
    browse the template gallery and download a Cornell Box or other example scene.

**Example with the Cornell Box template:**

```bash
./build/relwithdebinfo/skewer/skewer-render apps/scene-previewer/public/templates/scene.json
```

For all available flags (`--frame`, `--frames`, `--statics-only`), see the [CLI Reference](../reference/cli.md#skewer-render).

## 2. Run a Cloud Render

If you've [set up the Google Cloud render farm](gcp.md), the previewer's **Render** button
(top-left corner) dispatches renders to the cloud farm. Click the cloud icon (top-right)
to track progress.

For full previewer documentation, including local development setup, see the
**[Previewer Guide](../reference/previewer.md)**.

## Next Steps

- [Scene Format](../reference/scene-format.md) — Understanding scene JSON
- [Rendering Tips](../reference/rendering-tips.md) — Best practices for quality and performance
- [Animation](../reference/animation.md) — Keyframe animation and motion blur
- [Compositing](../developer/loom/index.md) — Layer compositing with loom
- [CLI Reference](../reference/cli.md) — Complete CLI documentation
- [GCP Deployment](gcp.md) — Cloud rendering setup
