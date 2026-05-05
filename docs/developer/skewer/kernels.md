# Kernels

Kernels in Skewer represent the low-level execution logic for the rendering process. They are designed to be high-performance and often contain the "hot loops" of the engine.

## Rendering Kernels

The core rendering logic is encapsulated in `skewer/src/kernels/path_kernel.cc`. This file contains the `Li()` function, which is the heart of the path tracer. 

### Why Kernels?
By separating the kernel (the mathematical logic of a single ray path) from the integrator (the orchestration of many rays), Skewer achieves:
- **Testability**: Individual kernels can be unit-tested with specific ray inputs.
- **Portability**: The kernel logic is written to be relatively independent of the threading model (though currently C++ based, this structure simplifies future explorations into GPU/SIMD optimization).

## Volume Dispatch

Volumetric sampling is dispatched through specialized kernels (`skewer/src/kernels/volume_dispatch.cc`). This layer handles the logic of choosing between homogeneous media sampling and heterogeneous (VDB) sampling based on the current `vol_stack` state.

## Visibility & Lighting Kernels

Utility kernels in `skewer/src/kernels/utils/` provide standardized implementations for:
- **Direct Lighting**: `GenerateLightSample` and shadow ray management.
- **Visibility**: `EvaluateVisibility` for calculating transmittance along shadow rays.
- **Volume Tracking**: `CalculateTransmittance` for Ratio Tracking vs. Woodcock Tracking.
