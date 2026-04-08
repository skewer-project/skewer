# The Go Control Plane: Architecture & Rationale

## 1. Introduction: The "Brain" of the Operation

In the Skewer rendering system, the **Go Control Plane** (specifically the **Coordinator**) acts as the central nervous system. While the C++ workers provide the raw computational power (the "Brawn") to trace rays and compute pixel values, the Go Control Plane provides the intelligence to organize, schedule, and manage the entire distributed process.

It effectively turns a collection of isolated dumb workers into a cohesive, fault-tolerant supercomputer.

## 2. Why Go? (Technical Rationale)

We chose Go (Golang) for the Control Plane not just for preference, but for specific architectural advantages that align perfectly with distributed systems:

### A. Concurrency Primitives (Goroutines)
Rendering high-resolution sequences often involves tracking thousands of inflight tiles and hundreds of workers simultaneously.
*   **The Problem:** C++ threads are heavy (OS-level) and expensive to context switch. Python's Global Interpreter Lock (GIL) limits true parallelism.
*   **The Go Solution:** Goroutines are lightweight (starting at ~2KB stack) and managed by the Go runtime. We can spawn a goroutine *per request* or *per heartbeat* for thousands of workers without breaking a sweat, ensuring the Coordinator never blocks.

### B. Cloud-Native Ecosystem
The modern cloud infrastructure stack (Kubernetes, Docker, Terraform, Prometheus) is built almost entirely in Go.
*   **Benefit:** Go has first-class, battle-tested libraries for interacting with:
    *   **Kubernetes (k8s client-go):** For programmatically spinning up pods.
    *   **Google Cloud Platform (GCP SDK):** For managing storage (GCS) and authentication.
    *   **Docker:** For container management.

### C. Safety & Reliability
The Coordinator is a long-running service. If it crashes, the entire render job halts.
*   **Memory Safety:** Go is garbage-collected and memory-safe. It eliminates entire classes of bugs (like buffer overflows or dangling pointers) that could cause a C++ orchestrator to segfault in the middle of a 10-hour job.
*   **Error Handling:** Go's explicit error handling (`if err != nil`) forces developers to handle network failures and edge cases upfront, which is critical in a distributed environment where "anything that can fail, will fail."

### D. gRPC & Protobuf Native
Since gRPC was born at Google (where Go is heavily used), the Go implementation of gRPC is incredibly robust and performant. It serves as the perfect "glue" language to define the Service Contracts that our C++ workers must adhere to.

## 3. Core Responsibilities

The Go Control Plane is responsible for three critical pillars of the system:

### Pillar 1: Job Orchestration
The Coordinator takes a high-level intent ("Render Frame 1") and breaks it down into atomic units of work.
*   **Task Generation:** Splits the image into tiles (e.g., 32x32 pixels) or separates render passes (Beauty, Depth, Normal).
*   **Scheduling:** Assigns these tasks to available workers based on locality or capacity.
*   **Retries:** If a worker fails to return a result within a timeout, the Coordinator re-queues the task for another worker, ensuring the job completes even if nodes die.

### Pillar 2: Worker Lifecycle & State Management
It acts as the "Source of Truth" for the cluster state.
*   **Registration:** Workers connect to the Coordinator on startup to announce their availability.
*   **Heartbeats:** Workers send periodic pulses. If a heartbeat is missed, the Coordinator marks the node as "Dead" and reschedules its work.
*   **Progress Tracking:** Aggregates status updates (e.g., "Job 45% complete") to report back to the user's CLI.

### Pillar 3: "Bring Your Own Project" (BYOP) Provisioning
This is the unique feature of Skewer's architecture. The Coordinator manages the cloud resources **on behalf of the user**.
*   **Credential Management:** It accepts Service Account keys securely.
*   **Resource provisioning:** It uses the Google Cloud SDK to spin up GKE Clusters or VM instances specifically inside the **user's** GCP project. This ensures the compute bill goes directly to the user, not the service provider.

## 4. Architecture Diagram

```mermaid
graph TD
    subgraph "User Local Machine"
        CLI[CLI Tool (C++)]
    end

    subgraph "Go Control Plane (The Brain)"
        COORD[Coordinator Service]
        SCHED[Task Scheduler]
        W_MGR[Worker Manager]
        BYOP[Cloud Provisioner]
        
        COORD --> SCHED
        COORD --> W_MGR
        COORD --> BYOP
    end

    subgraph "Data Plane (The Brawn)"
        W1[C++ Worker 1]
        W2[C++ Worker 2]
        W3[C++ Worker 3]
    end

    subgraph "Google Cloud"
        GCS[(GCS Bucket)]
        GKE[GKE Cluster]
    end

    %% Interactions
    CLI -- "1. Submit Job (gRPC)" --> COORD
    BYOP -- "2. Provision Nodes (REST)" --> GKE
    W1 -- "3. Register/Heartbeat (gRPC)" --> W_MGR
    SCHED -- "4. Assign Task (gRPC)" --> W1
    W1 -- "5. Upload Pixels" --> GCS
    W1 -- "6. Task Complete (gRPC)" --> SCHED
```

## 5. Summary

| Feature | Go Control Plane | C++ Data Plane |
| :--- | :--- | :--- |
| **Primary Goal** | Reliability & Orchestration | Raw Performance |
| **Key Resource** | Network I/O & Concurrency | CPU & Memory Bandwidth |
| **Failure Mode** | Must recover & retry | Can crash & restart |
| **Cloud Role** | Manager (API Calls) | Worker (Compute) |

By decoupling the "Brain" (Go) from the "Brawn" (C++), we create a system that is both **easy to manage** and **maximally performant**.
