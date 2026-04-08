# Skewer Control Plane Architecture: "Pull-Based" Design
**Date:** Feb 21, 2026
**Status:** Proposal

## 1. Executive Summary
We are designing the control plane for a distributed rendering system on Google Kubernetes Engine (GKE). The system must handle:
*   **Heavy Compute Tasks:** Rendering individual layers (Minutes to Hours).
*   **Deep Compositing:** Merging large EXR files (High Bandwidth).
*   **Spot Instances:** Running on preemptible nodes to save cost.
*   **Autoscaling:** Scaling from 0 to N workers based on demand.

**Core Decision:** We are moving from a "Push" model (Coordinator dials Workers) to a **"Pull" model** (Workers dial Coordinator). This simplifies network discovery, firewall traversal, and fault tolerance.

---

## 2. Component Architecture

### A. The Coordinator (Go)
*   **Role:** The "Brain" and State Manager.
*   **Deployment:** A single persistent `Deployment` in GKE (always on).
*   **Responsibility:**
    *   Accepts jobs from the CLI (`SubmitJob`).
    *   Decomposes jobs into atomic **Tasks** (Render Layer, Composite Frame).
    *   Maintains a **Task Queue** (e.g., in memory or Redis).
    *   Exposes metrics (e.g., `queue_length`) for the Autoscaler.
*   **Key Insight:** The Coordinator *does not* manage worker IPs or connections. It just manages the *List of Work*.

### B. The Worker (C++)
*   **Role:** The "Muscle".
*   **Deployment:** A scalable `Deployment` in GKE (starts at 0 replicas).
*   **Behavior:**
    *   Boots up.
    *   Connects to `skewer-coordinator:50051`.
    *   Enters a loop: `GetTask()` -> `DoWork()` -> `ReportSuccess()` -> Repeat.
    *   Can handle *both* Rendering and Compositing tasks (or specialized based on queue).
*   **Fault Tolerance:** If a Spot Instance is killed, the TCP connection drops. The Coordinator detects this (or the task times out) and re-queues the task for another worker.

### C. The Infrastructure (GKE + KEDA)
*   **Role:** The "Scaler".
*   **Mechanism:** **KEDA (Kubernetes Event-Driven Autoscaling)**.
*   **Logic:**
    *   Polls the Coordinator's metric: `queue_length`.
    *   **Scale Out:** If `queue_length > 0`, add pods.
    *   **Scale In:** If `queue_length == 0`, remove pods.
*   **Why:** This decouples the *application logic* (Go code) from the *infrastructure logic* (K8s API). The Coordinator doesn't need to know how to create Pods; it just publishes "I have work!"

---

## 3. Workflow walkthrough

1.  **Job Submission:**
    *   User runs `skewer render scene.obj`.
    *   CLI calls `Coordinator.SubmitJob`.
    *   Coordinator creates 10 "Render Layer" tasks and adds them to the queue.

2.  **Autoscaling Trigger:**
    *   KEDA sees `queue_length = 10`.
    *   KEDA updates the Worker Deployment to `replicas = 10`.
    *   GKE provisions nodes and starts 10 Pods.

3.  **Task Execution:**
    *   **Worker 1** boots up.
    *   **Worker 1** calls `GetTask(worker_id)`.
    *   **Coordinator** returns: `Task { Type: RENDER, Layer: "Background", Scene: "gs://..." }`.
    *   **Worker 1** downloads the scene, renders the layer, and uploads `background.exr` to GCS.
    *   **Worker 1** calls `UpdateTaskStatus(TaskID, Success, "gs://bucket/background.exr")`.

4.  **Job Completion:**
    *   Coordinator marks the task as done.
    *   When all layers for a frame are done, Coordinator creates a "Composite" task.
    *   A free worker picks up the Composite task, merges the EXRs, and uploads the final image.

---

## 4. Why this is the "Standard" approach
1.  **Simpler Networking:** Workers only need outbound internet access (to talk to Coordinator/GCS). No need for the Coordinator to know Worker IPs.
2.  **Robustness:** If a worker crashes, the task just sits in the queue until another worker picks it up. No complex "health check" logic needed in the Coordinator.
3.  **Cost Efficiency:** Using KEDA ensures you pay for *exactly* the compute you need. 0 items in queue = 0 workers running.

## 5. API Changes (Protobufs)
*   **Consolidate:** Move all RPCs to `coordinator.proto`.
*   **Remove:** `service Renderer` and `service Compositor` (Workers are no longer servers).
*   **Add:** `GetTask()` and `UpdateTaskStatus()` to `CoordinatorService`.
