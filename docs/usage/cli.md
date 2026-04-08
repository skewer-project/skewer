# CLI Reference

The `skewer-cli` is the command-line interface for interacting with the Coordinator to submit jobs, check their status, and cancel them.

## Commands

### submit

Submit a rendering job to the coordinator:

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

Check the status of a submitted job:

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

Cancel a running job:

```bash
./orchestration/cmd/cli/skewer-cli cancel --job <JOB_ID>
```

Drops all pending tasks and prevents further processing.

---

## Running Without Building

If you're iterating on the CLI code, run directly without compiling:

```bash
go run ./orchestration/cmd/cli/main.go help
```

---

## Auto Port-Forward

The CLI automatically handles `kubectl port-forward` if the coordinator isn't reachable on `localhost:50051`.
