# Comprehensive Architectural Deep Dive: GCP Serverless Rendering Framework

This document provides a detailed technical analysis of the serverless rendering pipeline implemented in PRs #176 and #186. It covers the implementation details, the underlying logic of the Google Cloud services used, and the architectural reasoning behind the migration from GKE/KEDA to this framework.

---

## 1. Architectural Philosophy: Managed State Machines

The fundamental shift in this migration is moving from **Stateful Polling** to **Managed Orchestration**.

### 1.1 The Legacy "Pull" Model (GKE/KEDA)
In the old system, the Go Coordinator maintained an in-memory queue of tasks. Worker pods in GKE would open long-lived gRPC streams (`GetWorkStream`) and wait for tasks.
- **Complexity:** Required managing Kubernetes node pools, Prometheus metrics, and KEDA ScaledObjects.
- **Cost:** Keeping a cluster "warm" resulted in wasted idle time.
- **Failure Mode:** If the Coordinator crashed, the in-memory queue was lost, and all active gRPC streams were severed.

### 1.2 The New "Batch" Model (Serverless)
The new system is **event-driven**. The Coordinator is now a purely reactive "bouncer" that validates requests and delegates the entire lifecycle of the render to **Cloud Workflows**.
- **Execution as State:** Every render job is a unique execution of a YAML-defined state machine.
- **Scale-to-Zero:** No compute resources exist until the Workflow explicitly requests them from the Cloud Batch API.

---

## 2. Orchestration: The Cloud Workflows DAG

The file `deployments/workflows/render_pipeline.yaml` is the "brain" of the engine. It implements a **Directed Acyclic Graph (DAG)** using Google's managed orchestration language.

### 2.1 The "Shatter" Pattern
When a job arrives, the workflow performs a "shatter" operation. It uses a `parallel` loop to iterate over the `layers` array passed by the Coordinator.
```yaml
- render_layers:
    parallel:
      shared: [layer_output_prefixes]
      for:
        value: layer
        in: ${args.layers}
        steps:
          - do_render:
              call: submit_batch_render
```
This allows the engine to render every layer of a frame (e.g., Character, Environment, Volumetrics) simultaneously on different physical hardware.

### 2.2 Content-Hash Caching Implementation
The workflow implements a distributed cache check *before* spending money on compute:
1.  **Key Generation:** The Coordinator generates a `cache_key` based on the content hash of the layer's geometry and material data.
2.  **Atomic Check:** The workflow calls `googleapis.storage.v1.objects.get` on the `cache` bucket.
3.  **Conditional Branching:** If the object exists, the workflow assigns the cached URI to the output list and skips the render step entirely.

### 2.3 State Polling Logic
Cloud Batch jobs are asynchronous. The workflow implements a custom `poll_batch_job` sub-workflow that uses an exponential backoff-style loop to check the `job.status.state`. This prevents the orchestration layer from timing out during long 4K render tasks.

---

## 3. Compute: Cloud Batch & Hardware Selection

Cloud Batch is used as a managed scheduler for the ephemeral C++ worker containers.

### 3.1 Skewer (Renderer) Implementation
- **Instance Type:** `n2d-highcpu-8`. These instances are powered by AMD EPYC processors, providing 8 vCPUs with a high clock speed optimized for the floating-point math required by the path tracer.
- **Provisioning Model: SPOT.** This is a critical cost choice. Spot instances utilize excess Google capacity at a 60-90% discount. 
- **Fault Tolerance:** Because Spot instances can be reclaimed by Google at any time, we set `maxRetryCount: 3` in the `taskSpec`. If a VM is preempted mid-render, Cloud Batch automatically provisions a new one and restarts the task.

### 3.2 Loom (Compositor) Implementation
- **Instance Type:** `e2-highmem-8`. Deep compositing is memory-intensive because Loom must load multiple large Deep EXR buffers into RAM to perform interval merging.
- **Provisioning Model: STANDARD.** Unlike Skewer, we do **not** use Spot instances for Loom. A preemption during the final composite phase is more expensive in terms of time, so we pay for a guaranteed Standard instance to ensure the merge completes successfully.

---

## 4. Data I/O: The GCS FUSE Abstraction

One of the most significant simplifications in this migration is how workers handle storage.

### 4.1 The Data Silo Problem
In traditional distributed rendering, you either have to bundle all textures/models into the Docker image (bloated images) or download them from a bucket at runtime (slow startup).

### 4.2 POSIX Mounting Logic
Terraform and Batch solve this using **GCS FUSE**. In `render_pipeline.yaml`, we define GCS volumes that are mounted to the container:
```yaml
volumes:
  - gcs:
      remotePath: ${data_bucket}
    mountPath: "/mnt/data"
```
The C++ worker binaries (`skewer-worker`) can then use standard C++ file I/O:
```cpp
std::ifstream scene_file("/mnt/data/scenes/forest.json");
```
**Technical Benefit:** This removes the need for the Google Cloud C++ SDK in the worker binaries, keeping them lightweight and portable. It also allows the OS kernel to handle file caching.

---

## 5. Security Architecture: The Terraform Layer

The infrastructure in `deployments/terraform/` is hardened to satisfy enterprise-grade security standards.

### 5.1 VPC Isolation (`network.tf`)
- **No Public IPs:** Worker VMs are created without external IP addresses. This makes them invisible to the public internet and prevents direct inbound attacks.
- **Private Google Access:** The subnet is configured with `private_ip_google_access = true`, allowing the VMs to reach `*.googleapis.com` internally.
- **Egress Whitelisting:** Firewall rules explicitly block all outbound traffic except for **Port 443 (HTTPS)**. Even if a malicious scene file managed to execute code on a worker, it could not reach a command-and-control server; it could only communicate with verified Google APIs.

### 5.2 IAM Principle of Least Privilege (`iam.tf`)
The migration replaced a single "Owner" service account with three distinct identities:
1.  **Coordinator SA:** Only allowed to `invoke` Workflows and write to GCS.
2.  **Workflow SA:** Only allowed to manage Batch jobs. It uses **Service Account User** permissions to "attach" the Batch Worker identity to the VMs it creates.
3.  **Batch Worker SA:** The only identity that can actually read scene data and write renders. It has no permission to create new resources or modify the infrastructure.

---

## 6. CI/CD: Automated Multi-Stage Lifecycle

The `deployments/cloudbuild.yaml` file automates the transition from source code to deployed infrastructure.

### 6.1 Multi-Stage Docker Optimization
The Dockerfiles for Skewer and Loom use a two-stage approach:
1.  **Build Stage:** Uses a heavy Ubuntu image with `clang-17`, `cmake`, and `vcpkg`.
2.  **Run Stage:** A minimal Ubuntu image containing only the essential runtime libraries (`libpng`, `libopenexr`).
**Result:** This reduces image size from ~2GB to ~150MB, significantly reducing "Pull Time" on Cloud Batch VMs and speeding up job startup.

### 6.2 Parallel Compilation
Cloud Build executes the `build-skewer` and `build-loom` steps concurrently on an `E2_HIGHCPU_8` machine. Since C++ compilation is highly parallelizable, this cuts the total build time by nearly half compared to sequential builds.
