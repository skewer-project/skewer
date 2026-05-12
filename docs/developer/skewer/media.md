# Media

Skewer supports physically-based volumetric rendering, allowing for complex light interactions inside fog, smoke, and clouds.

The volumetric system handles the complex interaction of light with participating media. Unlike solid surfaces, volumes require a **Continuum Model** where light can scatter at any point in 3D space.

A key architectural goal of Skewer is to treat volumes and surfaces as mathematically equivalent. By using **Stochastic Scattering**, the integrator sees a cloud particle exactly as it sees a triangle vertex. This allows us to use the same path tracing loop, NEE, and MIS logic for both, resulting in a cleaner and more maintainable codebase.

---

## Directory Reference

The following sections detail the implementations within the `skewer/src/media/` directory.

### Mediums

This header defines the core data structures for volumetric media and the bit-packing logic used for kernel dispatch.

#### Bit-Packed Medium IDs
To keep the `Ray` state small and the kernel dispatch fast, Skewer uses **Bit-Packed IDs** for media:

- **Bits 0-13**: The index into the scene's medium array.
- **Bits 14-15**: The `MediumType` (Vacuum, Homogeneous, Grid, NanoVDB).

This allows the `SampleMedium()` dispatcher to use a single integer switch statement, which is highly efficient for modern CPU branch predictors.

#### `HomogeneousMedium`
Represents media with constant density. It stores spectral absorption ($\sigma_a$) and scattering ($\sigma_s$) coefficients, along with the anisotropy parameter ($g$) for the Henyey-Greenstein phase function.

#### `GridMedium`
A simplified voxel-based medium used for procedural clouds or testing. It calculates density dynamically based on a bounding box and a noise function.

### NanoVDB Medium (Voxel Grids & Zero-Copy)

Skewer uses **NanoVDB** for high-performance, industry-standard volumetric grids.

#### Zero-Copy Architecture
Skewer is designed for **Cloud Rendering**, where memory overhead translates directly to dollar cost.

- **Memory Mapping (`mmap`)**: Skewer does not "load" VDB files into RAM in the traditional sense. It uses the `MappedFile` RAII wrapper to map the `.nvdb` file directly into the process's virtual address space.
- **Multi-Worker Efficiency**: On a high-core cloud node, multiple render threads can share the same physical memory pages for a 5GB voxel grid, vastly reducing the machine's RAM requirements.

#### AVX Alignment Requirements
NanoVDB is a pointer-less, flat data structure, but it requires **32-byte alignment** for SIMD (AVX) instructions.

- **Design Decision**: Skewer's loader scans the file for the grid's magic number. If the grid is 32-byte aligned in the file, we wrap it with zero-copy. If the file was exported poorly (non-aligned), we perform a single aligned copy. This ensures the renderer never segfaults regardless of the exporter used.
