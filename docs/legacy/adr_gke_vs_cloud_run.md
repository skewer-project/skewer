# Architecture Decision Record (ADR): GKE vs. Cloud Run

**Status:** Accepted
**Date:** 2026-02-20
**Context:** Deep Compositor "Skewer" Project

## The Question
Should the rendering and compositing workers run on **Cloud Run** (Serverless) or **GKE** (Google Kubernetes Engine)?

## The Decision
**We will use GKE (Google Kubernetes Engine).**

## The Rationale

### 1. Disk I/O & Memory Mapping (The Critical Constraint)
The **Deep Compositor** relies on processing massive `.kebab` and OpenEXR files (often exceeding 50GB per frame).
*   **Requirement:** The compositing algorithm uses `mmap` (memory mapping) to handle files larger than physical RAM. This requires a high-performance, addressable local filesystem.
*   **Cloud Run Failure:** Cloud Run uses an in-memory filesystem. Downloading a 50GB layer would require 50GB of RAM, causing an Out-Of-Memory (OOM) crash instantly. It does not support local SSDs for swap/cache.
*   **GKE Success:** GKE nodes support **Local NVMe SSDs**. We can utilize hundreds of GBs of high-speed scratch space for compositing without consuming RAM.

### 2. Execution Time Limits
*   **Requirement:** High-quality path tracing is computationally expensive. A single 4K frame can take hours to render.
*   **Cloud Run Failure:** Cloud Run has a hard timeout (typically 60 minutes). Jobs exceeding this are killed, resulting in lost work and wasted money.
*   **GKE Success:** Pods have no maximum execution time.

### 3. Cost Optimization (Spot Instances)
*   **Requirement:** Minimize the cost of compute.
*   **Comparison:** 
    *   **GKE Spot Nodes:** Offer 60-91% savings over on-demand pricing.
    *   **Cloud Run:** While efficient for "scale-to-zero" web services, the cost per vCPU-hour for sustained CPU usage is significantly higher than a GKE Spot VM.

### 4. Networking (gRPC)
*   **Requirement:** Long-lived, bidirectional gRPC streams between the Coordinator and Workers.
*   **Cloud Run Risk:** The managed load balancer often terminates idle connections or streams that persist too long, requiring complex reconnection logic.
*   **GKE Success:** Provides direct pod-to-pod networking or stable LoadBalancer services for persistent connections.

## Conclusion
While Cloud Run offers operational simplicity, it is architecturally incompatible with the **storage** and **duration** requirements of a production renderer. GKE provides the necessary hardware control (SSDs, Spot VMs) to build a cost-effective render farm.
