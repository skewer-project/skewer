# Limitations

This document lists the current limitations with the most recent version of the Skewer Project.

---

## Browser Support

The Scene Previewer uses the [File System Access API](https://developer.mozilla.org/en-US/docs/Web/API/File_System_Access_API) (`showDirectoryPicker`, `showOpenFilePicker`) to read and write scene files directly from the local filesystem. This API is only supported in **Chromium-based browsers** (Chrome, Edge).

- **Firefox:** Not supported at all.
- **Safari 15.4+:** Supported. No known issues.

## Scene Camera

The camera defined in `scene.json` is **static** — there is no camera animation path or moving camera support. The `look_from`, `look_at`, `vup`, and `vfov` values are fixed for the duration of a render.

The previewer viewport does provide interactive Three.js `OrbitControls` (orbit, pan, zoom) for inspection, but this does not affect the rendered output.

## Scene Format

- **Geometry:** Only three primitive types: `sphere`, `quad`, and `obj` mesh. No torus, cylinder, cone, NURBS, or other primitives.
- **Materials:** Only three material models — `lambertian`, `metal`, `dielectric`. No subsurface scattering, cloth, hair, volume scattering, or custom BSDFs.
- **Media/Volumes:** Only `nanovdb` type supported (`.nvdb` files). No procedural volumes, analytic fog, or heterogeneous media defined in-scene.
- **Mesh Import:** Only Wavefront `.obj` format supported. No glTF, FBX, USD, or other interchange formats.
- **Integrators:** Two choices — `path_trace` and `normals`. No photon mapping, bidirectional path tracing, VCM, or spectral MIS.

## Rendering

- **CPU-only:** Skewer is a CPU-based ray tracer. No GPU acceleration (though the data-oriented design was chosen to enable future GPU porting).
- **Spectral rendering:** Uses 4 wavelength samples per ray. No RGB rendering path.
- **Deep segment limits:** Maximum 16 deep segments per pixel, 16 depth buckets, 4 overlapping transmissive media.
- **Deep sample pool:** Capped at ~64 chunks (~1.8 GB). Exceeding this silently drops samples with a warning.
- **No AOV system:** No arbitrary output variables (albedo, normals, depth, etc. as separate channels). Each requires a separate render pass.
- **Spectral color:** Wavelength sampling uses a temporary approximation, not tabulated spectral data.
- **Fireflies:** Caustics, small emissive surfaces, and rough metals at grazing angles produce fireflies that cannot be fully eliminated. Reinhard tonemapping reduces but does not remove them.
- **No deferred differential geometry:** Surface interaction differentials (`dPdu`, `dPdv`, etc.) are not implemented, which limits texture filtering quality.
- **Motion blur:** Increases noise 2-4×, requiring significantly more samples.
- **Depth of Field:** Increases noise noticeably.
- **Render time:** Scales quadratically with resolution.

## Animation

- **No looping or ping-pong:** Animation clamps to the first and last keyframe values. No cycle modes.
- **Last keyframe curve ignored:** The interpolation curve on the final keyframe has no effect (no next keyframe to interpolate toward).
- **Expanded BVH bounds:** Animation requires expanded bounding boxes, which reduces acceleration structure efficiency.
- **Participating media is static:** The engine does not support the animation of nvdb media. Adding this is currently a work in progress.

## Cloud Platform

- **GCP-only:** The distributed pipeline is native to Google Cloud Platform (Cloud Run, Cloud Workflows, Cloud Batch, GCS). No AWS, Azure, or on-premise backends.
- **16-character layer ID limit:** Layer IDs (derived from filenames) must be ≤ 16 characters. Longer names cause GCP Batch job creation to fail with `INVALID_ARGUMENT`.
- **No underscores in filenames:** GCP Batch job IDs only allow lowercase letters, numbers, and hyphens. Underscores are converted but should be avoided.
- **SSD quota:** Default 300 GB SSD limit caps concurrent VMs at ~10 (30 GB boot disk each).
- **Spot VM delays:** Workers use SPOT instances by default. Provisioning may be delayed during high-demand periods.
- **Firebase authentication required:** The previewer and API require Firebase Authentication with Google Identity Platform. No anonymous or local-only mode.
- **Rate limits:** API limits `/v1/jobs/init` to 60/hr and `/v1/jobs/{id}/submit` to 5/hr per email by default.
- **File upload limits:** Maximum 2048 entries per job init request; paths limited to 512 characters.
- **Data retention:** Pipeline data retained for 30 days; cache for 90 days.

## Build System

- **No pre-built binaries:** Source build is required. No pre-compiled releases are available.
- **Toolchain requirements:** C++20 compiler, CMake 3.21+, Go 1.21+, protoc, and several system libraries (OpenEXR, Imath, Zlib, libpng).
- **Platform-specific optimizations:** Native CPU optimizations (`-march=znver3a`) only apply on Linux x86_64. Other platforms use a generic build.
- **Windows:** MSVC CRT conflict workaround required (`gtest_force_shared_crt`). Some flags (e.g., `-ffast-math`) are Clang/GCC-only.
