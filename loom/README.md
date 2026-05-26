# Loom

A deep compositor for merging rendered EXR layers with correct transparency handling.

> **Full documentation:** [skewer-project.github.io/skewer](https://skewer-project.github.io/skewer/)

## Quick Start

```bash
# 1. Clone the repository
git clone https://github.com/skewer-project/skewer.git
cd skewer

# 2. Install dependencies — see docs for your platform:
#    https://skewer-project.github.io/skewer/getting-started/installation/

# 3. Pull Git LFS assets (only what's needed to build)
git lfs install && git lfs pull --include="skewer/external/srgb_spec_data.h"

# 4. Build with CMake presets
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo --parallel 4

# 5. Composite rendered layers
./build/relwithdebinfo/loom/loom layer1.exr layer2.exr --output composited
```

Loom composites deep EXR layers front-to-back by depth, producing a flattened PNG
and an optional deep EXR output.

For the full compositing algorithm, CLI flags, and layer ordering details, see the
**[Loom Compositor docs](https://skewer-project.github.io/skewer/developer/loom/)**.

## Authors

- [AkshatAdsule](https://github.com/AkshatAdsule)
- [yooian](https://github.com/yooian)
- [shavolkov](https://github.com/shavolkov)
- [C3viche](https://github.com/C3viche)
