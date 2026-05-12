# Session & Execution

The `RenderSession` (`skewer/src/session/render_session.cc`) is the top-level orchestrator of the Skewer engine. It manages the lifecycle of a render, from parsing the user's JSON configuration to writing the final high-bit-depth pixels to disk.

In a cloud environment, compute nodes are ephemeral. Skewer's session architecture is built on the principle of **Statelessness**. By ensuring that a session can be interrupted and resumed without loss of data, we enable the use of low-cost, preemptible hardware, significantly reducing the cost of high-quality rendering.

The session explicitly separates **Data Resolution** (finding and loading assets) from **Numerical Integration** (the path tracer). This allows the engine to handle complex, distributed file systems (like GCS) while keeping the core rendering kernels focused purely on the mathematics of light.

---

## Directory Reference

The following sections detail the implementations within the `skewer/src/session/` directory.

### Render Session

The `RenderSession` class serves as the primary entry point for the engine. It coordinates the interactions between the `Scene`, `Integrator`, `Camera`, and `Film`.

A typical render session follows a strictly defined four-stage process:

1.  **Orchestration**: The session parses the `scene.json`. It identifies which layers need to be rendered and resolves the dependencies (context files).
2.  **Scene Synthesis**: It builds the scene for the current frame, constructing the TLAS/BLAS hierarchy and identifying all emissive light sources.
3.  **Parallel Integration**: The session hands off the scene to a multi-threaded `Integrator`. The integrator manages the CPU's thread pool, distributing work via a **Dynamic Tile Scheduler**.
4.  **Serialization**: The final beauty and deep buffers are extracted from the film and written to GCS or local storage.

The session also manages global initialization, such as `InitSpectralModel()`, which loads the embedded sRGB-to-Spectrum look-up tables during the session's constructor. This ensures that all components have access to the spectral foundations before the first ray is traced.


#### Multithreading & Work Distribution
Skewer is built for high-core-count machines. Rather than assigning rows of pixels to threads (which causes "idling" if one thread hits a complex object while another hits a blank background), Skewer uses **Work Stealing Tiles**:

- The image is divided into $32 \times 32$ tiles.
- Threads use a single `std::atomic<int> next_tile` to "claim" the next available block of work. This ensures efficient load balancing.

#### Design for Cloud Parallelism
The execution model is uniquely optimized for **Google Cloud Batch**.
- **Layer-Level Parallelism**: Skewer treats each scene layer as an independent unit of work. This allows the cloud orchestrator to spin up separate VMs for each layer, enabling complex frames to be rendered in minutes.
- **Statelessness & GCS FUSE**: The `RenderSession` is entirely stateless. All assets are read from a virtualized filesystem (`/mnt/gcs/`) and outputs are written directly back to the cloud bucket, supporting seamless **Preemption** and restarts on cheap Spot VMs.

### Render Options

This header defines the `RenderOptions` and `IntegratorConfig` structures, which serve as the unified configuration interface for the engine.

#### Adaptive Logic Control
The session act as the control center for the adaptive sampling algorithm.

- **`IntegratorConfig`**: Stores user-defined parameters like `noise_threshold`, `min_samples`, and `adaptive_step`.
- **Optimization**: By controlling these parameters, the session balances the overhead of convergence checks against the computational savings of stopping early on "easy" pixels.
