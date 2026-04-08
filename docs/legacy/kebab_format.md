# Kebab File Format (.kebab) - Architectural Strategy

The Kebab format is a specialized, flat-binary deep image format designed for high-performance distributed compositing. Unlike OpenEXR, which is optimized for storage and network efficiency, Kebab is optimized for **memory-mapped random access** during the merging process.

## 1. Why Kebab? (The "RAM Cheat Code")
Deep images are massive. A single 1080p frame with 10 samples per pixel can exceed 500MB. Merging 20+ layers in a compositor would normally require tens of gigabytes of RAM.

### The Storage vs. Processing Trade-off
- **OpenEXR (The "Shipping Container"):** Render workers output OpenEXR because its high compression (PIZ/DWAA) drastically reduces cloud storage costs and network egress time (GCS to Compositor). It is the ideal format for "shipping" data across the cloud.
- **Kebab (The "Workbench"):** Once on the compositor's local NVMe SSD, we "thaw" EXRs into Kebab files. Kebab uses a flat layout that matches the in-memory `DeepSample` structure. This allows us to use `mmap()` to map the file directly into the process's address space.

### Memory Mapping (`mmap`)
- **Lazy Loading:** The OS only loads a pixel's data into RAM at the exact moment the compositor accesses it.
- **Virtual Memory:** We can "open" 100GB of deep data on a machine with only 4GB of RAM. The OS manages the "conveyor belt" of data from disk to RAM.
- **No Decompression Overhead:** Unlike EXR, there is no CPU-heavy decompression step between the disk and the compositor logic.

## 2. Storage Efficiency (Kebab V2)
To keep disk usage low on VMs without losing `mmap` compatibility, we use **structural optimization** rather than algorithmic compression (like ZIP).

### Optimized `DeepSample` Struct
| Field | Type | Size | Notes |
| :--- | :--- | :--- | :--- |
| `z_front` | `float32` | 4 bytes | Hard surface/volume start. |
| `z_back` | `float32` | 4 bytes | Volume end depth. |
| `color` | `half[3]` | 6 bytes | 16-bit floats (saves 6 bytes vs 32-bit). |
| `alpha` | `uint8_t` | 1 byte | Quantized 0.0-1.0 (saves 3 bytes). |
| **Total** | | **15 bytes** | roughly 40% smaller than the original 24-byte struct. |

### Offset Table
The `pixelOffsets_` array is stored as `uint32_t` instead of `uint64_t`. 
*   **Capacity:** Supports up to 4.2 billion total samples (more than enough for 4K deep images).
*   **Savings:** 4 bytes saved per pixel (~32MB for a 4K image).

## 3. The "Hybrid" Workflow: Streaming Thaw
To avoid "Memory Peaks" (crashing while loading a large EXR), we use a scanline-based streaming approach to convert EXR to Kebab.

### Algorithm:
1.  **Open EXR** for reading.
2.  **Open Kebab** for writing.
3.  **For each scanline (y):**
    *   Read one row of pixels from EXR into a small temporary buffer.
    *   Convert the EXR data into the optimized `DeepSample` structs.
    *   Append the structs to the Kebab file.
    *   Free the temporary row buffer.
4.  **Finalize Header:** Write the final `pixelOffsets_` and `totalSamples` count.

**Result:** The memory footprint remains constant (only one scanline in RAM) regardless of the total image size.

## 4. Implementation Guidelines
*   **Versioning:** Every `.kebab` file must include a `version` and `structSize` in the header. If the C++ `DeepSample` struct changes, the loader must reject old files to prevent segmentation faults.
*   **Alignment:** Ensure the `DeepSample` struct is appropriately packed/aligned for the target architecture to avoid performance penalties during `mmap` access.
