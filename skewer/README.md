# Skewer

A physically-based ray tracer capable of deep rendering and compositing.

> **Full documentation:** [skewer-project.github.io/skewer](https://skewer-project.github.io/skewer/)

## Quick Start

```bash
# 1. Clone the repository
git clone https://github.com/skewer-project/skewer.git
cd skewer

# 2. Install dependencies — see docs for your platform:
#    https://skewer-project.github.io/skewer/getting-started/installation/

# 3. Pull Git LFS assets (only what's needed to build)
git lfs install && git lfs pull --include="skewer/src/core/spectral/srgb_spec_data.cc"

# 4. Build with CMake presets
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo --parallel 4

# 5. Render a scene
./build/relwithdebinfo/skewer/skewer-render scene.json
```

The renderer outputs a `.png` file for preview and an `.exr` file for compositing.

For scene examples, CLI flags (`--frame`, `--frames`, `--statics-only`), and rendering tips,
see the **[CLI Reference](https://skewer-project.github.io/skewer/reference/cli/)** and
**[Rendering Tips](https://skewer-project.github.io/skewer/reference/rendering-tips/)**.

## Authors

- [AkshatAdsule](https://github.com/AkshatAdsule)
- [yooian](https://github.com/yooian)
- [shavolkov](https://github.com/shavolkov)
- [C3viche](https://github.com/C3viche)
