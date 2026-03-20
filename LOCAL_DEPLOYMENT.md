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
*(Note: Your `data/` directory and rendered images are safely preserved on your host machine.)*

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

#### 1. Submitting a Job (`submit`)

The `submit` command packages your rendering parameters and sends them to the Coordinator. The coordinator will break the job down into atomic tasks and distribute them to the workers.

**Basic Submission (Using Scene Defaults):**
```bash
./apps/cli/skewer-cli submit \
  --scene data/scenes/panda-####.json \
  --frames 4 \
  --output data/renders/test_panda/
```
*Note: Using `####` in the filename tells the coordinator to render a sequence of frames (e.g., `panda-0001.json`, `panda-0002.json`).*

**Advanced Submission (Overriding Quality Settings):**
You can override the JSON scene file's defaults using CLI flags:
```bash
./apps/cli/skewer-cli submit \
  --scene data/scenes/panda-####.json \
  --frames 4 \
  --samples 256 \
  --width 1920 \
  --height 1080 \
  --deep \
  --output data/renders/test_panda/
```

**Key Flags:**
*   `-s, --scene`: URI of the scene file (local `data/` path or `gs://`). **[Required]**
*   `-f, --frames`: Number of frames to render.
*   `-S, --samples`: Maximum samples per pixel (overrides JSON if > 0).
*   `-W, --width` / `-H, --height`: Image dimensions (overrides JSON if > 0).
*   `--deep`: Enable deep EXR output for compositing.

*When successful, the CLI will output your newly generated `Job ID`.*

#### 2. Checking Job Status (`status`)

To monitor the progress of a submitted job, use the `status` command and pass the `Job ID`:

```bash
./apps/cli/skewer-cli status --job <YOUR_JOB_ID>
```

**Example Output:**
```
[CLI] Coordinator not reachable. Attempting to port-forward...
[CLI]: Port-forwarding successful.
Job Status: JOB_STATUS_RUNNING
Progress: 25.0%
```

#### 3. Canceling a Job (`cancel`)

If you need to stop a long-running render, you can kill it in the Coordinator:

```bash
./apps/cli/skewer-cli cancel --job <YOUR_JOB_ID>
```
This drops all pending tasks for the job and prevents further processing.

---

## Local Data Mapping

The Skewer cluster is configured to mount your local repository's `data/` directory directly into the worker containers using a Kubernetes `hostPath` volume. 

This means:
1.  Place your `.json` scenes and `.obj` assets inside `data/scenes/` or `data/objects/`.
2.  Set your output directory to something inside `data/renders/`.
3.  The workers will read and write files directly to your host machine, making it easy to inspect the final `.png` or `.exr` results instantly!