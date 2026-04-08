# Skewer Control Flow: Distributed Rendering

This document explains how the different services in Skewer interact when a user initiates a render job.

## 1. High-Level Flowchart

```mermaid
sequenceDiagram
    participant User as User (CLI)
    participant Coord as Coordinator (Go - Local)
    participant GCP as GCP Compute (C++ Workers)
    participant GCS as GCS Bucket (Storage)

    User->>Coord: 1. Execute "skewer render"
    Note over User,Coord: CLI automatically spawns Coordinator process
    
    Coord->>Coord: 2. Parse Scene & Auth Keys
    Coord->>GCP: 3. Provision Render Workers (GCE VMs/GKE)
    
    loop Rendering
        Coord->>GCP: 4. Send RenderLayerRequest (gRPC)
        GCP->>GCP: 5. Path Tracing (C++)
        GCP->>GCS: 6. Upload Deep Layer (.kebab)
        GCP-->>Coord: 7. Return Result URI
    end

    Coord->>Coord: 8. Execute Local Compositor
    Coord->>GCS: 9. Download & Merge Layers
    Coord-->>User: 10. Final Image Output (.exr/.png)
    
    Coord->>GCP: 11. Teardown Resources (Shutdown VMs)
```

## 2. Component Responsibilities

### A. The CLI Entry Point (`apps/cli/`)
*   **Location:** User's Local machine.
*   **Role:** The user's interface. When run, it checks if a local Coordinator is active; if not, it spawns one as a background process.
*   **Lifecycle:** Stays alive as long as the user wants to monitor the render.

### B. The Coordinator (`apps/coordinator/`)
*   **Location:** User's Local machine.
*   **Role:** The "Orchestrator." 
    *   Reads the user's GCP Service Account key.
    *   Calls GCP APIs to spin up high-performance VMs.
    *   Slices the image into layers/tasks and distributes them via gRPC.
    *   Ensures workers are shut down after completion to save the user money.

### C. The Worker (`apps/worker/`)
*   **Location:** User's Google Cloud Project (Remote VMs).
*   **Role:** The "Computation Engine."
    *   Receives render tasks via gRPC.
    *   Renders high-quality path-traced layers.
    *   Writes heavy data directly to GCS to avoid network bottlenecks.

### D. The Storage (GCS)
*   **Location:** User's Google Cloud Project.
*   **Role:** The "Data Hub."
    *   Acts as a high-bandwidth intermediate buffer between remote workers and the local compositor.

## 3. Automatic Lifecycle Management

The user should not have to manually start services. The following logic is implemented in the CLI:

1.  **Dependency Check:** Does the user have `gcloud` configured or a JSON key provided?
2.  **Process Forking:** The CLI uses system calls to start the Go Coordinator in the background if it's not already running.
3.  **Heartbeat:** The CLI communicates with the Coordinator via a local gRPC port (e.g., `:50051`).
4.  **Auto-Cleanup:** When the Coordinator finishes its task, it sends a shutdown signal to the cloud workers before exiting itself.
