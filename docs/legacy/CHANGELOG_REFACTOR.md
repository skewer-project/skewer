# Skewer Distributed Rendering: Technical Refactor Deep-Dive

This document provides a detailed technical explanation of the architectural changes made to stabilize the Skewer distributed rendering pipeline.

---

## 1. Memory Architecture & Stability (The "OOMKill" Fixes)

### The Problem: Memory Multipliers
In a standard render, every sample takes up space. In a **Deep Render**, every ray hit at every depth is recorded. 
*   **The Math:** An 800x450 image has 360,000 pixels. At 50 samples per pixel, that's 18 million potential samples. 
*   **The Spike:** Previously, the system tried to store all 18 million samples in a single contiguous list and then copy/sort them all at once. This caused peak memory usage to exceed 4GB, triggering a Kubernetes `OOMKill` (Out of Memory).

### The Solution: `DeepSegmentPool` (Chunked & Capped)
We moved from a giant `std::vector` to a **Chunked Pool Allocator** (`skewer/src/film/deep_segment_pool.h`).
*   **How it works:** Memory is allocated in 1MB "chunks" (~32,000 nodes each) only when needed. This prevents a massive upfront 2GB allocation.
*   **Thread Safety:** Since multiple render threads call `Allocate()` simultaneously, we use an `std::atomic<size_t> cursor` for lightning-fast increments, and a `std::mutex` only when the pool needs to grow a new chunk.
*   **Safety Ceiling:** The pool is capped at **64 chunks (~1.8GB)**. If a scene is too complex, `AddDeepSample` now prints a warning and gracefully drops the extra samples instead of crashing the pod.

### Transient Memory: Scanline processing
Even with a stable pool, the **Export Phase** (`BuildDeepImage`) was crashing.
*   **Old Way:** Copy all 18 million samples into a temporary buffer -> Sort them -> Save. (Peak RAM: ~5GB).
*   **New Way:** We process the image **one scanline at a time**. We only ever copy/sort the samples for a single row of pixels. 
*   **Result:** Transient memory usage dropped from Gigabytes to Megabytes.

---

## 2. Distributed Workflow Logic

### The "Intermediate Chunk" Requirement
Distributed rendering works by splitting samples (e.g., 1000 samples split into 4 workers doing 250 each).
*   **The Constraint:** To merge these 4 results correctly, Loom **must** have the depth data. If workers saved flat PNGs, Loom couldn't "weld" the samples together.
*   **The Fix:** Updated `internal/coordinator/server.go`. If `sample_division > 1`, the coordinator automatically overrides the task to `enable_deep = true`. 
*   **Smart Extension:** Both Skewer and Loom now check the `output_uri`.
    *   `.exr` -> Save raw Deep/Flat data.
    *   `.png` -> Automatically call `FlattenPhase` to convert deep samples into a displayable image.

### Path Safety: `ensureDirectoryExists`
Renderers often failed because `/data/renders/my_job/` didn't exist yet.
*   Added a utility in `libs/exrio/src/utils.cc` that recursively creates the parent directory tree (using `std::filesystem::create_directories`) before any file write occurs.

---

## 3. Coordinator Reliability & Scaling

### Fix: The "Ghost Task" Cancellation Bug
There was a bug in `scheduler.go` where `PurgeJobTasks` used a loop like `for i := 0; i < len(ch); i++`. 
*   **The Bug:** When a task was removed, `len(ch)` got smaller, causing the loop to finish early and skip the rest of the queue.
*   **The Fix:** We now capture `qLen := len(ch)` before the loop starts, ensuring every single task in the queue is checked.

### Independent Scaling (Skewer vs. Loom)
Previously, if there were 10 Render tasks, the Coordinator would spawn 10 Skewer workers AND 10 Loom workers.
*   **Refactor:** The Coordinator now queries `GetQueueLengths()` (plural). It calculates `skewerTarget` and `loomTarget` separately. 
*   **Benefit:** Loom workers now only spin up when there is actual merging to do, saving huge amounts of RAM on your host machine.

### MAX_WORKERS Cap
Updated `deployments/k8s/coordinator.yaml` with `MAX_WORKERS: "2"`. 
*   This is a global governor. Even if you submit a 100-frame job, the coordinator will only allow 2 pods to exist at once, protecting your laptop from crashing.

---

## 4. CLI & Infrastructure

*   **`--threads` / `-t`:** Passes a CPU thread limit from the CLI to the C++ worker. Useful for limiting resource usage per pod.
*   **`--deep`:** A flag for the `submit` command to tell the whole pipeline you want a Deep EXR as the final result.
*   **`scripts/gen_proto.sh`:** A convenience script to run `protoc` with the correct paths for this project.
