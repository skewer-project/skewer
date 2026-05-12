# Quick Start

This guide walks through your first render with Skewer. See [Installation](installation.md) and [Building](building.md) for detailed setup instructions.

## Prerequisites

Make sure you've completed the installation steps:

1. Installed system dependencies
2. Built the project with CMake
3. Built the Go CLI

## 1. Run a Local Render

The simplest way to render a scene is to run the built binary directly:

```bash
# Using the built binary
./build/relwithdebinfo/skewer/skewer-render <path-to-scene.json>
```

!!! tip "Getting a scene"
    Use the [Scene Previewer](http://skewer.pages.dev) to quickly bootstrap and edit an example scene with the Cornell Box

**Example with a sample scene:**

```bash
./build/relwithdebinfo/skewer/skewer-render apps/scene-previewer/public/templates/scene.json
```

### Command-Line Options

| Flag        | Description                              |
| ----------- | ---------------------------------------- |
| `--scene`   | Path to scene JSON file **\[Required\]** |
| `--output`  | Output image path (PNG or EXR)           |
| `--width`   | Override image width                     |
| `--height`  | Override image height                    |
| `--samples` | Override max samples per pixel           |
| `--threads` | Number of render threads (0 = auto)      |

## 2. Using the Scene Previewer

The web-based previewer lets you visualize scenes before rendering:

```bash
cd apps/scene-previewer
bun install
bun run dev
```

Open http://localhost:5173 to view the previewer.

### Previewer Features

- **Open Existing Scene** - Load folders containing layer-format scenes
- **Layer Navigation** - Browse contexts and layers in the sidebar
- **Object Selection** - Click objects in the viewport or sidebar to select
- **Property Editing** - Modify sphere center/radius, quad vertices, obj transforms
- **Material Editing** - Edit albedo, roughness, emission, IOR per material
- **Delete Objects** - Press `Delete` or `Backspace` to remove selected objects

## 3. Running a Distributed Render

The easiest way to run a distributed render is through the **Scene Previewer**.
If you've correctly [setup Google Cloud, Firebase, and the Scene Previewer](../deployment/gcp.md),
there should be a `Render` button in the **top left corner** of the previewer with a scene open.

### Check Status

Click on the Cloud icon on the **top right** of the Scene Previewer to track the progress of current and previous cloud renders.

## Next Steps

- [Scene Format](../reference/scene-format.md) - Understanding scene JSON
- [Rendering Tips](../reference/rendering-tips.md) - Best practices for quality and performance
- [Animation](../reference/animation.md) - Keyframe animation and motion blur
- [Compositing](../reference/compositing.md) - Layer compositing with loom
- [CLI Reference](../reference/cli.md) - Complete CLI documentation
- [Architecture](../architecture/overview.md) - How the system works
- [Deployment](../deployment/local.md) - Local cluster setup
- [GCP Deployment](../deployment/gcp.md) - Cloud rendering setup
- [GKE Deployment](../legacy/gke.md) - Running on Google Kubernetes Engine