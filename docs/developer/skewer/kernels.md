# Rendering Kernels

In Skewer, **Kernels** are the self-contained, stateless mathematical functions that evaluate a single ray path. They are the "hot loop" of the engine and are designed for extreme performance. They encapsulate the low-level physics and math of light-matter interaction in a format that is isolated from the system's high-level orchestration:

- **Integrators** (`src/integrators/`): Manage the "Outer Loops" (threading, tiling, adaptive breaks).
- **Kernels** (`src/kernels/`): Manage the "Inner Loops" (ray math, spectral accumulation, scattering).

This separation allows the core math to be tested and profiled in isolation. It also ensures that if we port Skewer to **SIMD (AVX-512)** or **GPU (CUDA)** in the future, the integrator orchestration remains unchanged while only the kernels are re-implemented.

---

## Directory Reference

The following sections detail the implementations within the `skewer/src/kernels/` directory.

### Path Kernel

The `Li()` function is the engine's most critical code path. For every bounce, it performs a complex state-machine check:

1. **Intersection**: Query the BVH for the closest surface.
2. **Volume Dispatch**: If the ray is in a medium, sample the volume up to the surface hit.
3. **Scatter Decision**: Decide if the ray hit a particle (volume scatter) or the surface.
4. **Lighting**: Evaluate Next Event Estimation (NEE).
5. **Bounce**: Update the throughput and generate the next ray direction.

The path kernel is an **iterative** path tracer. Recursive path tracers suffer from stack overflow issues and are difficult to optimize for modern CPUs. Skewer uses a `while` loop that maintains a `beta` (throughput) spectrum and a `L` (accumulated radiance) spectrum. 

The path kernel is also **stateless**. It only communicates with the `SampleWriter` and `RNG`.

#### The Rendering Equation
The core of our path tracer is the evaluation of the Kajiya Rendering Equation:

$$
L_o(p, \omega_o) = L_e(p, \omega_o) + \int_{\mathcal{S}^2} f_r(p, \omega_i, \omega_o) L_i(p, \omega_i) \cos \theta_i \mathrm{d}\omega_i
$$

We use Monte Carlo integration to approximate this integral by tracing thousands of random paths.

#### Reducing Variance (Noise)
1. **Next Event Estimation (NEE)**: At every surface interaction, Skewer explicitly samples a light source to find bright sources faster than random bouncing.
2. **Multiple Importance Sampling (MIS)**: Combines BSDF sampling and NEE using the **Power Heuristic** ($\beta=2$) to weight contributions based on sampling efficiency.
3. **Russian Roulette (RR)**: Probabilistic termination after the 3rd bounce to save computation on low-energy paths while remaining unbiased. This acts as an early-exit optimization for negligible ray contributions


### Sample Media (Volumetric Integration)

This module implements the physics of light scattering within participating media.

- **`SampleHomogeneous`**: Fast, analytical Beer-Lambert solving for uniform media.
- **`SampleGrid` / `SampleNanoVDB`**: Stochastic Woodcock tracking inside a voxel grid to find discrete scattering points.

#### Beer-Lambert Law
Light passing through a volume (like fog) is attenuated exponentially based on density and distance.

$$
T_r(d) = e^{-\sigma_t \cdot d}
$$

<figure align="center">
  <svg width="400" height="180" viewBox="0 0 400 180" xmlns="http://www.w3.org/2000/svg">
    <path d="M 50 150 L 350 150" stroke="currentColor" stroke-width="1" />
    <path d="M 50 150 L 50 30" stroke="currentColor" stroke-width="1" />
    <path d="M 50 30 C 150 30, 200 140, 350 145" stroke="#00c853" stroke-width="2" fill="none" />
    <text x="130" y="45" fill="#00c853" font-size="12">Transmittance</text>
    <text x="320" y="165" fill="currentColor" font-size="10">Distance (d)</text>
  </svg>
</figure>


#### Woodcock Tracking (Delta Tracking)
To render non-uniform volumes (VDB clouds), we use Woodcock tracking. It uses a "majorant" (maximum density) to probabilistically decide whether a photon collides with a particle or passes through as a "null collision."


### Volume Dispatch

A specialized kernel that routes the ray to the correct sampling algorithm based on the bit-packed ID in the `vol_stack`. This layer handles the logic of choosing between homogeneous media sampling and heterogeneous (VDB) sampling without using polymorphic virtual calls.

### Utils

- **`direct_lighting.h`**: Implements Next Event Estimation (NEE). It handles light selection and shadow ray generation.
- **`visibility.cc`**: Calculates shadow visibility, continuing rays through transparent surfaces and accumulating spectral transmittance.
- **`volume_tracking.cc`**: Implements **Ratio Tracking** for shadow rays, ensuring smooth, noise-free volumetric shadows.

#### Henyey-Greenstein Phase Function (Ratio Tracking)
Unlike surfaces that use BSDFs, volumes use **Phase Functions** to describe scattering. We implement the Henyey-Greenstein function to model anisotropic scattering (light bending forward or backward).

<figure align="center">
  <svg width="400" height="150" viewBox="0 0 400 150" xmlns="http://www.w3.org/2000/svg">
    <ellipse cx="230" cy="75" rx="60" ry="30" fill="none" stroke="#ffab40" stroke-width="2" />
    <circle cx="170" cy="75" r="5" fill="currentColor" />
    <path d="M 100 75 L 160 75" stroke="currentColor" stroke-width="1" stroke-dasharray="2"/>
    <text x="285" y="55" fill="#ffab40" font-size="12">Forward (g > 0)</text>
    <text x="130" y="90" fill="currentColor" font-size="10">Particle</text>
  </svg>
</figure>
