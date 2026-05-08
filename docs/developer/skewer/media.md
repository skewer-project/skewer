# Participating Media (Volumes)

Skewer supports rendering participating media (fog, smoke, clouds) using physically-based volumetric integration (`skewer/src/kernels/sample_media.cc` and `skewer/src/kernels/utils/volume_tracking.cc`).

## Tracking Algorithms

The core challenge in volume rendering is determining how far a ray travels before hitting a particle. Skewer uses different algorithms depending on the use case.

### Woodcock Tracking (Delta Tracking)
Used for finding discrete scattering events in heterogeneous media (VDBs).
- **Majorant**: Skewer calculates a "majorant" (the maximum possible extinction coefficient in the volume).
- **Probabilistic Stepping**: The ray takes steps based on the majorant. At each step, we "accept" the collision with a probability of $\frac{\sigma_t(x)}{\bar{\sigma}_t}$. If rejected, it's a "null collision," and the ray continues.

### Ratio Tracking (Shadow Rays)
Used for estimating transmittance along shadow rays.
- **Why?**: Woodcock tracking is stochastic and can be noisy for visibility checks. 
- **Mechanism**: Instead of accepting/rejecting a collision, Ratio Tracking treats the volume as partially transparent at every step. It continuously multiplies the transmittance $T_r$ by the probability of a null collision: $T_r \cdot (1 - \sigma_t(x)/\bar{\sigma}_t)$. This provides a much smoother, unbiased estimate for shadows.

## NanoVDB Integration

Skewer uses **NanoVDB** for high-performance volumetric grids (`skewer/src/media/nano_vdb_medium.h`).

### Zero-Copy Loading
To minimize memory overhead and startup time on Cloud Batch, Skewer uses **Memory Mapping (`mmap`)**:
1. The `.nvdb` file is mapped directly into the process's virtual memory space as read-only.
2. **32-Byte Alignment**: Skewer detects if the grid data in the file is 32-byte aligned. If it is, the engine wraps the raw pointer directly (zero-copy). If not, it performs a single aligned copy to prevent AVX/SIMD segmentation faults during traversal.

### VDB Accessors
NanoVDB accessors are small, stack-allocated objects that cache hierarchical nodes. Skewer uses these to perform $O(1)$ density lookups at any world-space point during the tracking loop.

## Phase Functions
Unlike surfaces that use BSDFs, volumes use **Phase Functions** to describe scattering. Skewer implements the **Henyey-Greenstein** function, which uses an anisotropy parameter $g$ to model forward ($g > 0$) or backward ($g < 0$) scattering.
