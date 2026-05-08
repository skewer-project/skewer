# Scene Graph & Scene Loading

Skewer uses a hierarchical scene graph to represent the 3D environment and a specialized loading pipeline to convert JSON descriptions into optimized internal structures.

## Scene Graph (`skewer/src/scene/`)

The scene graph manages the lifecycle of:
- **Objects**: Primitives (spheres, quads) and complex meshes.
- **Lights**: Emissive surfaces that contribute to direct lighting (NEE).
- **Materials & Textures**: Defining surface appearance.
- **Media**: Volumetric grids (NanoVDB) or homogeneous volumes.

### Instancing
The scene graph supports instancing through the TLAS/BLAS hierarchy. A `SceneNode` can be a group (containing children) or a leaf (containing geometry). Transformations are propagated through the graph during the `Build()` phase.

## Scene Loading (JSON Pipeline)

The `SceneLoader` (`skewer/src/io/scene_loader.cc`) handles the multi-file parsing strategy:

1. **Scene File (`scene.json`)**: Contains global settings like the camera, output directory, and a list of **Layer** files and **Context** files.
2. **Context Files**: Shared definitions of materials and media that can be reused across multiple layers.
3. **Layer Files**: Define the actual geometry and specific render options for a subset of the scene (e.g., a "Character" layer).

### Asset Loading
- **OBJs**: Loaded via `obj_loader.cc`, which parses geometry and automatically bakes it into the `Triangle` format for the BVH.
- **NanoVDB**: Loaded via memory-mapping to ensure zero-copy performance on cloud workers.
- **Textures**: Loaded through a unified `Texture` system that handles PNG and other common formats.
