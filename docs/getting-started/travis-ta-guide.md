# Travis and TA Quick Start — Interactive Grading

This is a streamlined guide for TAs and faculty to render scenes using Skewer.
Your email has been registered for our cloud render farm. Google Chrome is all you need.

## Step 1: Download a scene file

Sample scenes are available on Google Drive:

**[Download sample scenes →](https://drive.google.com/drive/u/2/folders/1rdr24XMyvrBft8MjJE8zhBiQZQIvqQUB)**

Pick any scene and download the entire directory.
Any static scene is a good starting point.

## Step 2: Load it in the hosted previewer

1. Open the **[hosted Scene Previewer](https://skewer.pages.dev)** (Chrome or Edge only)
2. Click "Sign in" on the top right to authenticate with the Google account you registered with us
2. Click **Open Scene** and select the JSON file you downloaded
3. Use the mouse to orbit, pan, and zoom around the 3D view

!!! info "Using the previewer"
    For a full walkthrough of the previewer interface (layer controls, material editing,
    camera positioning) see the **[Previewer Guide](../reference/previewer.md)**.

## Step 3: Edit if you want

The previewer lets you make last-minute adjustments before rendering:

- **Move objects** — select them in the sidebar or 3D view and drag, rotate, or scale
- **Tweak materials** — change material type, albedo, roughness, IOR, or emission
- **Adjust render settings** — resolution, integrator, sample count

All changes are saved back to the scene JSON when you download.

## Step 4: Render on the cloud

Once your scene looks right, click the **Render** button in the top-left corner
of the previewer. This dispatches your scene to the cloud render farm.

Track progress by clicking the cloud icon in the top-right corner.
When the render completes, the output files are available to view and download in the cloud dashboard.

!!! tip "Cloud rendering details"
    For more on how the cloud pipeline works, see the
    **[GCP Deployment Guide](gcp.md)**.

## Optional: Render headlessly on your machine

If you'd prefer to render locally, download the pre-built binaries:

1. Grab `skewer-render` for your platform from the **[Releases page](https://github.com/skewer-project/skewer/releases)**
2. Run it on your scene:

    ```bash
    ./skewer-render path/to/scene.json
    ```

!!! tip "Full binary instructions"
    For download links, checksum verification, and platform-specific notes, see the
    **[Pre-built Binaries](release-binaries.md)** guide.

## Need help?

- [Scene Format Reference](../reference/scene-format.md) — Understanding the scene JSON structure
- [Rendering Tips](../reference/rendering-tips.md) — Quality and performance advice
- [CLI Reference](../reference/cli.md) — All `skewer-render` flags
