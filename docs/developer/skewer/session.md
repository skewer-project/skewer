# Render Session & Execution

The `RenderSession` (`skewer/src/session/render_session.cc`) is the top-level orchestration layer for a rendering task.

## Render Lifecycle

1. **Initialization**: The session parses the scene file and initializes the spectral model.
2. **Setup**: It loads the required layers and context files, building the BVH for each unique mesh (BLAS) and the top-level instance tree (TLAS).
3. **Adaptive Rendering**: If a `noise_threshold` is set, the session configures adaptive sampling, where Skewer focuses compute power on high-variance regions of the image.
4. **Integration**: The session hands off control to the `Integrator` (e.g., `PathTrace`), which manages the thread pool and tile distribution.
5. **Output**: Once the render loop completes, the film builds the final image (beauty pass and/or deep EXR) and the session saves the files to the output directory.

## Cloud Batch Integration

The `RenderSession` is designed to run in ephemeral environments like **Google Cloud Batch**:
- **Layer-Level Parallelism**: Every layer of a scene can be rendered by a different physical VM.
- **Statelessness**: The session is entirely stateless; all inputs are read from GCS (via GCS FUSE), and all outputs are written back to GCS.
- **Fault Tolerance**: Because sessions are stateless and deterministic (given the same seed), Cloud Batch can safely preempt a worker and restart it without losing data (provided the workflow orchestrator manages the retries).
