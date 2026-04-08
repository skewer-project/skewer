# Local Deployment & CLI Guide

This guide outlines how to deploy the Skewer distributed rendering engine locally and how to use the `skewer-cli` to submit and manage rendering jobs.

## Prerequisites & Dependencies

To run the Skewer cluster locally, you will need the following tools installed:

*   **Docker CLI**: Required to build the container images for the coordinator and workers.
*   **Kubernetes Environment**:
    *   **[OrbStack](https://orbstack.dev/)** (Recommended for macOS): A fast, lightweight drop-in replacement for Docker Desktop that includes a built-in Kubernetes cluster.
    *   *Alternatively*, **[Minikube](https://minikube.sigs.k8s.io/docs/start/)** can be used.
*   **kubectl**: The Kubernetes command-line tool, used by the deployment script to apply manifests and check cluster status.
*   **Go (1.21+)**: Required to build the CLI and the Go Coordinator.

## Starting the Local Cluster

The deployment is fully automated using the provided bash script. This script will build the Docker images, start your local Kubernetes environment (OrbStack or Minikube), and deploy the Coordinator, Skewer Worker, and Loom Worker deployments.

From the root of the project, run:

```bash
./deployments/deploy_local.sh --rebuild
```

**Flags:**
*   `--rebuild`: Forces a rebuild of the Docker images and the Go CLI binary. Use this whenever you modify C++ or Go source code.
*   `--workers <number>`: Sets the global limit on the number of workers that can be scaled up (default is 4). Example: `--workers 8`.
*   `--runtime <orbstack|minikube>`: Specify your local Kubernetes runtime (defaults to `orbstack`).

**To stop the local cluster and clean up resources:**
```bash
./deployments/stop_local.sh
```
*(Note: Your repository files (scenes, renders, etc.) on the host are not removed by this script.)*

## Using the Skewer CLI

The `skewer-cli` is the command-line interface for interacting with the Coordinator to submit jobs, check their status, and cancel them. The CLI automatically handles `kubectl port-forward` behind the scenes if the coordinator isn't immediately reachable on `localhost:50051`.

### Option A: Using the Compiled Binary (Recommended)
The `deploy_local.sh --rebuild` script automatically compiles the binary and places it at `apps/cli/skewer-cli`.

You can run it directly:
```bash
./apps/cli/skewer-cli help
```

### Option B: Using `go run`
If you are iterating rapidly on the Go CLI code, you can run it directly without compiling a persistent binary:
```bash
go run apps/cli/main.go help
```

---

### Common CLI Commands

#### 1. Rendering a Scene (`render`)

The `render` command packages your rendering parameters and sends them to the Coordinator. The coordinator will break the job down into atomic tasks and distribute them to the compute workers.

**Basic Rendering (Using Scene Defaults):**
```bash
./skewer-cli render \
  --name my_panda_render \
  --scene data/scenes/panda-####.json \
  --frames 4 \
  --output data/renders/test_panda/
```
*Note: Using `####` in the filename tells the coordinator to render a sequence of frames (e.g., `panda-0001.json`, `panda-0002.json`).*

**Advanced Rendering (Overriding Quality Settings):**
You can override the JSON scene file's defaults using CLI flags:
```bash
./skewer-cli render \
  --name high_res_panda \
  --scene data/scenes/panda-####.json \
  --frames 4 \
  --samples 256 \
  --width 1920 \
  --height 1080 \
  --deep \
  --output data/renders/test_panda/
```

**Key Flags:**
*   `-s, --scene`: URI of the scene file (local path with optional `data/` prefix, or `gs://`). **[Required]**
*   `-f, --frames`: Number of frames to render.
*   `-S, --samples`: Maximum samples per pixel (overrides JSON if > 0).
*   `-W, --width` / `-H, --height`: Image dimensions (overrides JSON if > 0).
*   `--deep`: Enable deep EXR output for compositing.

*When successful, the CLI will output your newly generated `Job ID`.*

#### 2. Deep Compositing (`composite`)

The `composite` command tells the Loom workers to physically merge multiple Deep EXR layers (e.g. merging a dynamic character's frames with a static background). The resulting output is a standard **Flat EXR** 2D image, which can be viewed natively in macOS Preview or using professional tools like [DJV Imaging](https://darbyjohnston.github.io/DJV/).

**Method A: Using Job Dependencies**
If you just rendered two scenes via Skewer, you can automatically composite them by linking their Job IDs. The Coordinator will automatically evaluate where they saved their files:
```bash
./skewer-cli composite \
  --name auto_composite \
  --depends-on <JOB_ID_1>,<JOB_ID_2> \
  --output data/renders/final_image/
```

**Method B: Using Existing Disk Files (`--layers`)**
If you have existing EXRs, provide a comma-separated list of folder prefixes. 
Use the pipe `|` to specify the extension and frame count limit `path/prefix|.ext|frames`:
```bash
./skewer-cli composite \
  --name explicit_composite \
  --layers "data/renders/pandas|.exr|10,data/renders/cloud|.exr|1" \
  --frames 10 \
  --output data/renders/custom_final/
```
*In the example above, the compositor processes 10 consecutive frames from `pandas`, continually merging them over the exact same single `frame-0001` from the `cloud` background.*

#### 3. Checking Job Status (`status`)

To monitor the progress of a submitted job, use the `status` command and pass the `Job ID`:

```bash
./skewer-cli status --job <YOUR_JOB_ID>
```

**Example Output:**
```
[CLI] Coordinator not reachable. Attempting to port-forward...
[CLI]: Port-forwarding successful.
Job Status: JOB_STATUS_RUNNING
Progress: 25.0%
```

#### 4. Canceling a Job (`cancel`)

If you need to stop a long-running render, you can kill it in the Coordinator:

```bash
./skewer-cli cancel --job <YOUR_JOB_ID>
```
This drops all pending tasks for the job and prevents further processing.

---

## Local Data Mapping

The Skewer cluster mounts your **repository root** into worker and coordinator containers at `/data` using a Kubernetes `hostPath` volume. You can pass either `data/scenes/foo.json` or `scenes/foo.json`: the coordinator rewrites them to `/data/scenes/foo.json` in the cluster. The same applies to `--output` (`data/renders/` is the default prefix).

This means:
1.  Keep `.json` scenes and `.obj` assets under `scenes/` and `objects/` at the repo root (or use the same layout with a `data/` prefix in URIs).
2.  Set your output directory to something like `data/renders/my_job/` (written under `renders/` in the repo).
3.  Workers read and write files on your host so you can inspect `.png` or `.exr` results immediately.