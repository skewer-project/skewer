# Input / Output (IO)

The IO system manages the transition of data between the filesystem (local or cloud) and the internal engine memory.

## Scene Serialization

Skewer uses a JSON-based scene format powered by `nlohmann/json`. The IO layer is responsible for:
- **Path Resolution**: Resolving relative paths (textures, meshes) based on the location of the scene file.
- **Spectral Conversion**: Converting RGB colors found in JSON into `SpectralCurve` objects for the spectral rendering core.

## Image IO

The `image_io.cc` system handles writing the final render results:
- **LDR (Low Dynamic Range)**: Writing PNG files for quick previews.
- **HDR (High Dynamic Range)**: Writing standard OpenEXR files.
- **Deep Data**: Integrating with `exrio` to write Deep EXR files, including the complex step of grouping and merging stochastic path segments.

## Mesh & Volume IO

- **OBJ Loader**: A custom, fast OBJ parser that extracts vertices, normals, and UVs, and immediately "bakes" them into the `Triangle` format required by the renderer.
- **NanoVDB IO**: Specialized memory-mapped IO for volumetric grids, ensuring that multi-gigabyte clouds can be accessed with zero-copy overhead.
