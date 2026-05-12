# Core Math & Spectral Systems

The `core` directory houses the math, sampling, and spectral representations that all other systems rely on. Every other module must respect the mathematical invariants and data representations established here.

Generally, these are shared by multiple different systems. Module-specific classes, math, and code implementations may be found in more specific directories.


## Directory Reference

The following sections detail the implementations within the `skewer/src/core/` directory.

## Color
Contains the RGB struct and helper functions. 

Skewer maintains a strict separation between color representations. RGB is used for loading and saving of images, while actual rendering utilizes physically-based spectral light.

## Containers

To avoid the overhead of standard library containers (which often involve heavy heap allocations and complex iterator logic), Skewer uses two specialized, cache-friendly containers for the hot-path rendering loop.

### `BoundedArray<T, N>`
Located in `core/containers/bounded_array.h`, this is a fixed-capacity wrapper around a raw C-style array.

- **Stack-Only**: It never allocates on the heap.
- **Zero Overhead**: It stores the count as a `size_t` alongside the data, providing a `std::vector`-like API without the dynamic growth logic.
- **Use Case**: Ideal for small, hard-capped lists where the maximum size is known at compile time, such as the `kNSamples` in a spectral packet or the `kMaxDeepSegments` (16) stored by the `SampleWriter`.

### `SmallVector<T, N>`
Located in `core/containers/small_vector.h`, this is a "hybrid" container that implements **Small Buffer Optimization (SBO)**.

- **Inline Storage**: It contains an internal array of size `N`. If you add `N` or fewer elements, it stays on the stack (or inline within its parent struct).
- **Heap Spilling**: If the count exceeds `N`, it dynamically allocates a heap buffer to hold the overflow.
- **Design Rationale**: This is designed for "Power Law" distributions. For example, during deep image merging, 90% of pixels might only contain 1 or 2 segments (fitting in inline storage), while 10% of pixels (overlapping edges or volumes) might contain 100+ segments. `SmallVector` allows the renderer to be efficient for the common case without failing on the complex case.

## Math

### Vector Operations
Most of our spatial calculations rely on standard 3D vector algebra.

- **Dot Product ($A \cdot B$):** Used to calculate the cosine of the angle between vectors, essential for Lambertian shading and visibility checks.
- **Cross Product ($A \times B$):** Used to generate orthogonal vectors, such as calculating surface normals from triangle edges.

### Transformations & Quaternions
Skewer avoids 4x4 transformation matrices in favor of the **TRS (Translation, Rotation, Scale)** structure.

In deep hierarchies (e.g., a hand on an arm on a body), 4x4 matrix multiplications accumulate floating-point error, causing "shearing" or "exploding" geometry. TRS structures separate these components:

- **Quaternions:** (`core/math/quat.h`) Used for rotations to avoid gimbal lock and ensure smooth animation interpolation. Quarternions are easily re-normalized (`QuatNormalize`) to maintain unit length, ensuring perfectly orthogonal rotation frames.
- **Normal Transformation**: Non-uniform scaling requires the inverse-transpose of the transformation. For a matrix, this is expensive. In Skewer, we compute this analytically: `(1.0/scale)` followed by the rotation.
We use a **TRS (Translation, Rotation, Scale)** system to place objects in the world.
- **SLERP (Spherical Linear Interpolation):** Used to interpolate between two rotation keyframes along the shortest path on a 4D unit sphere.

### Orthonormal Basis (ONB)
To sample directions on a surface, we construct a local coordinate system (tangent space) where the surface normal is the Z-axis. When a ray hits a surface, we use an ONB to transform a randomly sampled "hemisphere" direction back into world space.

## Monte Carlo Sampling & Randomness

### PCG32 Random Number Generator
Skewer uses the **PCG32** algorithm instead of the standard `mt19937`.

- **Performance**: PCG32 is significantly faster and has a much smaller state (16 bytes), allowing it to be stored directly on the stack or in the `Ray` state.
- **Deterministic Parallelism**: Every pixel is seeded deterministically using its $(x, y)$ coordinate and the current `sample_index`. This ensures that a render is mathematically identical regardless of thread count or task scheduling, which is critical for debugging cloud batch workers.

### Multiple Importance Sampling (MIS)
To reduce noise, we combine two sampling techniques: **Next Event Estimation (NEE)** (sampling lights directly) and **BSDF Sampling** (following the material's physical properties). We weight them using the **Power Heuristic** ($\beta=2$):

$$
w_f(p) = \frac{f(p)^\beta}{f(p)^\beta + g(p)^\beta}
$$

### Volumetric Stack (`VolumeStack`)

The `VolumeStack` (`core/sampling/volume_stack.h`) is a priority-based set that tracks the nested media a ray is currently inside (e.g., Camera -> Fog -> Glass -> Water).

- **Priority Logic**: When geometry overlaps, the medium with the highest `priority` value is considered the "active" one.
- **Optimization**: The stack is kept sorted by priority at insertion time. This makes `GetActiveMedium()` an $O(1)$ operation. Since this is queried thousands of times per ray during marching, we optimize for the read rather than the write.

### Wavelength Sampler
Skewer uses a **Stratified Hero Wavelength Sampler** (`core/sampling/wavelength_sampler.h`) to sample the visible spectrum (360nm to 830nm). We sample 4 wavelengths per ray. One is the "Hero," used to make discrete decisions (like reflecting vs refracting), while the others ("Companions") are evaluated at the same spatial path to minimize variance.

- **Hero Wavelength:** For every ray, one wavelength is sampled uniformly at random from the visible range. This is the "Hero" wavelength.
- **Stratification:** The remaining `kNSamples - 1` wavelengths (companions) are chosen by adding a fixed delta ($\Delta = \text{range} / kNSamples$) to the Hero's wavelength and wrapping around the visible range if necessary.
- **Unbiased Estimation:** Each wavelength is assigned a PDF of $1 / \text{range}$, ensuring that the Monte Carlo estimator remains unbiased.

This ensures that each ray covers a well-distributed set of wavelengths, reducing "color noise" (chromatic aliasing) while allowing for efficient SIMD processing of the spectral packet.

This is based on the paper **Hero Wavelength Spectral Sampling** by A. Wilkie, S. Nawaz, M. Droske, A. Weidlich, and J. Hanika 

## Spectral

Skewer is a **Spectral Renderer**. It does not perform internal calculations in RGB; it simulates actual wavelengths of light.

### `SpectralCurve` & `Spectrum`
- **`SpectralCurve`**: A lightweight representation of a material's reflectance or emission across the visible spectrum (380nm to 780nm), stored as coefficients.
- **`Spectrum` (SpectralPacket)**: An `alignas(16)` packet of **4 wavelengths** (`kNSamples = 4`) that are traced simultaneously. This allows us to use SIMD instructions to perform 4x the work per ray.

### `RGB2Spec` Integration
Since most input data (textures/colors) is in sRGB, Skewer uses a precomputed table-lookup system (`core/spectral/spectral_utils.h`) to convert linear sRGB into the most physically plausible spectral curve, minimizing color bias during integration. 

This is based on the paper **A Low-Dimensional Function Space for Efficient Spectral Upsampling** by Wenzel Jakob and Johannes Hanika
