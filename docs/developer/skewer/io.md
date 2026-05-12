# Input / Output (IO) & Asset Loading

The IO system bridges the gap between high-level scene descriptions and the optimized binary memory structures used by the engine core. It manages the complex mapping between external, artist-friendly formats and the internal, performance-optimized data layouts of the renderer.

---

## 2. Directory Reference

The following sections detail the implementations within the `skewer/src/io/` directory.

### Scene Loader

Skewer implements a **Tiered JSON Architecture**, which allows artists and developers to build complex environments from reusable pieces. The scene loader acts as a high-level parser for JSON scene files.

- **Scene File (`scene.json`)**: The "Master" file. It defines the global camera, the output directory, and the temporal animation range.
- **Context Files**: Contain shared definitions for materials and media. This allows a single "Material Library" file to be referenced by hundreds of different scene layers.
- **Layer Files**: Define the actual geometry and per-layer render overrides.

This modular approach was chosen to support parallel production pipelines: A character artist can work on `character_context.json` while a lighting artist works on `light_layer.json`, and they can be merged at render time without file conflicts. 

### Graph From JSON (Transformation & Curve Parsing)

This module handles the low-level parsing of spatial data and animation curves from JSON. It translates JSON arrays into **TRS** (Translation, Rotation, Scale) structures and identifies **Bezier Curve** presets (ease-in, ease-out) for temporal interpolation.

### Object Loader

Skewer uses a custom bridge to **TinyOBJLoader**. Since OBJs are a legacy format, Skewer must translate their material fields into modern PBR (Physically Based Rendering) values.

- **Conversion Logic**:
    - **Metallic Path**: If `mtl.metallic >= 0.5`, Skewer treats the material as a pure metal and ignores standard diffuse values.
    - **Glass Path**: If `mtl.dissolve < 0.99` or the illumination model is set to glass, Skewer automatically creates a `Dielectric` material with an IOR of 1.5 (default).
- **Auto-Fit**: Skewer includes an `auto_fit` flag that normalizes incoming OBJ geometry to a 2-unit cube. This prevents issues where models exported in millimeters appear 1000x larger than models exported in meters.

### Image I/O

This system manages high-performance image serialization, integrating directly with the **OpenEXR** library.

- **OpenEXR (Standard & Deep)**:
    - **Scanline vs. Tiled**: For output, Skewer writes scanline-based files. This is a deliberate choice for **Deep Data**, as scanline files are significantly more compatible with post-production compositors like Nuke.
    - **DWA Compression**: While currently writing uncompressed floats for precision, the IO layer is architected to support lossy DWA compression for smaller file sizes in production environments.
- **LDR Previews (stb_image)**: For quick look-dev, Skewer uses `stb_image` and `stb_image_write` to export standard PNG files. The `image_io` system handles the conversion from high-dynamic-range (HDR) radiance to low-dynamic-range (LDR) pixels using a standard **sRGB Gamma 2.2** curve.

---

## Threading Considerations

All scene loading and BVH construction are performed **sequentially** on the main thread during the initialization phase. Once the scene is "Built" and the read-only memory structures are ready, the multi-threaded rendering phase begins. This ensures that no mutex locks are required during the time-critical render loop.
