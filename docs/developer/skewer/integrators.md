# Integrators & Path Tracing

Integrators are the "brain" of the rendering process, simulating how light travels from emitters to the camera.

## Path Tracing Loop

Skewer implements a tile-based, iterative path tracer (`skewer/src/kernels/path_kernel.cc`).

### The Rendering Equation
We approximate the Rendering Equation using Monte Carlo integration. For each pixel, multiple random paths are traced, and their radiance is averaged.

### Key Components of `Li()`
- **Next Event Estimation (NEE)**: At every bounce, the integrator explicitly samples a light source (`GenerateLightSample`) and shoots a shadow ray to calculate direct lighting. This significantly reduces noise compared to purely random path sampling.
- **Multiple Importance Sampling (MIS)**: Skewer uses the **Power Heuristic** ($\beta=2$) to weight the contributions of BSDF sampling and Light sampling.
- **Russian Roulette**: To prevent infinite recursion and save computation on low-energy paths, Skewer uses probabilistic path termination after the 3rd bounce.
- **Medium Transitions**: The integrator maintains a `vol_stack` to track when a ray enters or exits a medium boundary (e.g., entering a glass of water).

## Deep Path Recording
Unlike standard renderers that store a single color per pixel, Skewer's path tracer uses a `DeepPathRecorder` to capture the radiance and alpha at every interaction point along the ray. This data is then deferred and resolved into the `DeepSegmentPool` in the film.
