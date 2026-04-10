# Quick Start

This guide walks through your first render with Skewer. See (Installation)[installation.md] and (Building)[building.md] for instructions on installing dependencies to get started.

## 1. Build the Project

```bash
cmake --preset release
cmake --build --preset release --parallel
```

## 2. Run a Local Render

The simplest way to render a scene:

```bash
# Using the built binary
./build/relwithdebinfo/skewer/skewer-render --scene data/scenes/panda-0001.json --output output.png
```

## 3. Submit a Distributed Job

### Start Local Cluster

```bash
./deployments/deploy_local.sh --rebuild
```

This starts:
- Coordinator (port 50051)
- Skewer workers
- Loom workers

### Submit a Job

```bash
./orchestration/cmd/cli/skewer-cli submit \
  --scene data/scenes/panda-####.json \
  --frames 4 \
  --output data/renders/my_job/
```

### Check Status

```bash
./orchestration/cmd/cli/skewer-cli status --job <JOB_ID>
```

### Cancel if Needed

```bash
./orchestration/cmd/cli/skewer-cli cancel --job <JOB_ID>
```

## 4. Using the Scene Previewer

The web-based previewer lets you visualize scenes before rendering:

```bash
# (After merging PRs #136-138)
cd apps/scene-previewer
bun install
bun run dev
```

Open http://localhost:5173 to view the previewer.

## Next Steps

- [CLI Reference](../usage/cli.md) - Complete CLI documentation
- [Scene Format](../usage/scene-format.md) - Understanding scene JSON
- [Architecture](../architecture/overview.md) - How the system works
