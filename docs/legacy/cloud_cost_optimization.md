# Cloud Cost Optimization Strategy

Rendering on the cloud can be expensive, but with smart architectural choices, you can slash costs by **60-90%** without sacrificing render quality.

This guide outlines specific strategies for the Skewer system (Go Coordinator + C++ Workers) to minimize your Google Cloud Platform (GCP) bill.

## 1. Compute: The "Spot" Revolution (60-91% Savings)

The single biggest cost saver is using **Spot VMs** (formerly Preemptible VMs). These are spare Google Cloud capacity offered at a massive discount.

*   **The Catch:** Google can reclaim these instances at any time with a 30-second warning.
*   **The Skewer Solution:** Our Go Coordinator is designed for this.
    *   **Fault Tolerance:** If a worker node disappears (preempted), the Coordinator detects the missing heartbeat.
    *   **Retry Logic:** The task (e.g., "Render Tile 4 of Frame 10") is simply re-queued and assigned to another worker.
*   **Performance Impact:** Moderate. You might see occasional "hiccups" where a tile takes longer because it had to be restarted, but the total throughput remains high.
*   **Action Item:** Always configure your GKE Node Pools to use `spot: true`.

### 2. Granularity: Small Tiles Save Money

When a Spot VM is preempted, you lose the work currently in progress on that node.

*   **Bad Strategy:** Assigning one whole frame per worker. If the worker dies at 99% completion, you wasted minutes of compute time and money.
*   **Good Strategy:** Breaking frames into small **tiles** (e.g., 32x32 pixels).
    *   If a worker dies, you only lose a few seconds of work (that specific tile).
    *   **Bonus:** This also improves load balancing across the fleet.

## 3. Storage & Network: The "Data Gravity" Rule

Deep EXR files and `.kebab` files are massive (often gigabytes per frame). Moving them costs money (Network Egress) and storing them costs money (GCS Storage).

### A. Region Locality (Free Bandwidth)
**Rule:** Keep your Compute (GKE) and Storage (GCS) in the **same region** (e.g., `us-central1`).
*   **Scenario A (Bad):** GKE in `us-east1`, Bucket in `us-west1`. You pay per GB to move data across the country.
*   **Scenario B (Good):** GKE in `us-central1`, Bucket in `us-central1`. Data transfer between your workers and your bucket is usually **free** or significantly cheaper.

### B. Compression (The "Shipping Container" Strategy)
Never store raw uncompressed floats.
*   **Use DWAA/DWAB Compression:** For OpenEXR, this lossy compression is visually lossless for rendering but reduces file size by ~70-80% compared to uncompressed.
*   **Impact:** Reduces GCS storage fees *and* faster network transfers (less waiting for I/O).

### C. Download Only What You Need
Do not download the entire intermediate deep sequence to your laptop.
1.  **Render** on Cloud -> GCS.
2.  **Composite** on Cloud (using a cloud-based Compositor worker).
3.  **Download** only the final `beauty.png` or flattened `.exr`.
    *   **Savings:** Downloading a 5MB PNG is free/cheap. Downloading 500GB of deep data to your home internet will incur significant Egress charges.

## 4. Lifecycle Policies (Auto-Cleanup)

Rendering generates a lot of "trash" (intermediate files, test frames).

*   **The Feature:** GCS Object Lifecycle Management.
*   **Configuration:** Set a policy on your `skewer-temporary` bucket to **auto-delete objects older than 3 days**.
*   **Why:** You don't want to pay monthly storage fees for a test render you looked at once and forgot about.

## 5. Summary Checklist for the User

| Strategy | Estimated Savings | Impact on Workflow |
| :--- | :--- | :--- |
| **Use Spot Instances** | 60-90% | Occasional retries (handled auto). |
| **Same-Region GKE & GCS** | 100% of Inter-zone fees | None (just configuration). |
| **Aggressive Compression** | 70% Storage/Egress | None (visually identical). |
| **Auto-Delete Policy** | Variable | Must save "Keepers" to a different bucket. |
| **Small Tile Size** | Prevents wasted work | Slightly more coordination overhead. |

By following these rules, Skewer becomes a highly cost-effective "Personal Render Farm" that rivals commercial services.
