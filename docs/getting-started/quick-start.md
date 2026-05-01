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
./build/relwithdebinfo/skewer/skewer-render --scene <path-to-scene.json> --output output.png
```

**Example with a sample scene:**

```bash
./build/relwithdebinfo/skewer/skewer-render \
  --scene apps/scene-previewer/public/templates/scene.json \
  --output output.png
```

### Command-Line Options

| Flag | Description |
|------|-------------|
| `--scene` | Path to scene JSON file **[Required]** |
| `--output` | Output image path (PNG or EXR) |
| `--width` | Override image width |
| `--height` | Override image height |
| `--samples` | Override max samples per pixel |
| `--threads` | Number of render threads (0 = auto) |

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

## 3. Running a Distributed Render (GKE)

For distributed rendering on Google Kubernetes Engine:

### Prerequisites
- `gcloud` CLI installed and configured
- A GKE cluster with the coordinator and workers deployed

### Submit a Job

```bash
# Build the CLI first if not already built
go build -o skewer-cli ./orchestration/cmd/cli/

# Submit a render job
./skewer-cli submit \
  --scene gs://your-bucket/scenes/scene-####.json \
  --frames 4 \
  --output gs://your-bucket/renders/my_job/
```

### Check Status

```bash
./skewer-cli status --job <JOB_ID>
```

### Cancel a Job

```bash
./skewer-cli cancel --job <JOB_ID>
```

## 4. Local Development (Docker)

For local distributed testing:

```bash
# From the project root
docker compose up -d
```

This starts:
- Coordinator (port 50051)
- Skewer workers
- Loom workers

## Next Steps

- [Scene Format](../usage/scene-format.md) - Understanding scene JSON
- [Rendering Tips](../usage/rendering-tips.md) - Best practices for quality and performance
- [Animation](../usage/animation.md) - Keyframe animation and motion blur
- [Compositing](../usage/compositing.md) - Layer compositing with loom
- [CLI Reference](../usage/cli.md) - Complete CLI documentation
- [Architecture](../architecture/overview.md) - How the system works
- [Deployment](../deployment/local.md) - Local cluster setup
- [GCP Deployment](../deployment/gcp.md) - Cloud rendering setup
- [GKE Deployment](../deployment/gke.md) - Running on Google Kubernetes Engine