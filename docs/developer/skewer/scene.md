# Scene Graph & Management

The `scene/` directory contains the logic for representing the 3D world. Skewer uses a hierarchical scene graph that is "compiled" into a flat, traversal-ready structure during the `Build()` phase.

The `Build()` phase is a destructive process that flattens the hierarchy, bakes transforms, and generates acceleration structures. This design ensures that the renderer never has to perform complex graph lookups or matrix concatenations during the hot tracing loop.

---

## Directory Reference

The following sections detail the implementations within the `skewer/src/scene/` directory.

### Scene

This module implements the primary `Scene` class and the `Build()` orchestration logic.

#### The Build Phase
When `Build()` is called, the engine performs a depth-first traversal of the graph.

- **Transform Propagation**: Parent transforms are concatenated to children using `TRS::Compose`.
- **Instance Extraction**: Leaf nodes (Meshes/Spheres) are converted into `Instance` objects.
- **Light Identification**: Every triangle and sphere is checked for an emissive material. If emissive, it is added to a global `lights_` array for Next Event Estimation (NEE).

### Scene Graph

Defines the `SceneNode` structure used to build the hierarchical representation of the world. It supports `Group`, `Mesh`, and `Sphere` node types, each capable of carrying its own `AnimatedTransform`.

### Animation

Skewer implements a sophisticated temporal engine to support production-quality motion blur.

#### Animated Transforms
Each node can have an `AnimatedTransform` containing multiple keyframes.

- **Interpolation**: For any time $t$, Skewer calculates the transform using **Spherical Linear Interpolation (Slerp)** for rotations and **Cubic Bezier Curves** for translation/scale.
- **Evaluation**: The `EvaluateTransformChain` function recursively computes the world-space transform for any instance at a specific shutter time.

### Interpolation Curves (Easing)

Provides the mathematical foundation for non-linear animation.

- **Custom Easing**: Through `InterpolationCurve`, Skewer supports industry-standard easing (Ease-In, Ease-Out) rather than just simple linear motion.
- **Bezier Implementation**: Animations follow Bezier paths. Since time ($u$) is linear but the curve parameter ($t$) is not, we use the **Newton-Raphson Method** to iteratively solve for $t$ for a given normalized time $u$ such that $X(t) = u$.

### Camera

The `Camera` class handles the transformation from image-space coordinates to world-space rays.

- **The Shutter Window**: Motion blur is defined by the `shutter_open` and `shutter_close` properties. When a ray is generated, it is assigned a random `ray.time()` within this window.
- **Depth of Field**: Supports a thin-lens model with variable `aperture_radius` and `focus_distance`.

### 3.6 Light (Emission Management)

For Next Event Estimation (NEE) to work, the engine must be able to pick a random light source efficiently.

- **Light List**: Skewer groups all emissive primitives into a unified `lights_` array.
- **Sampling**: During rendering, we use the `InvLightCount()` to scale the contribution of a chosen light, ensuring the Monte Carlo estimator remains unbiased.
- **Area Normalization**: Skewer calculates the exact surface area of every light primitive (Triangle/Sphere) to ensure that larger lights correctly contribute more energy to the scene.

### Skybox (Environment)

Handles the evaluation of infinite environment light. In the current version, this is stubbed to provide a background radiance for rays that miss all geometry.

### Mesh Utils

Provides utility functions for generating standard geometric shapes, such as `CreateQuad()`, which automatically bakes geometry into the engine's internal format.
