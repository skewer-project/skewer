# Blender Tools

Skewer provides tools to work with Blender for scene creation and conversion.

## Blender to Skewer

Export Blender scenes to Skewer JSON format.

### Usage

1. Open Blender with your scene loaded
2. Go to the **Scripting** tab
3. Open `scripts/blender/blender_to_skewer/blender_export.py`
4. Run the script

The script exports:
- A full `scene.json` file
- All referenced assets

to your downloads folder.

---

## Skewer to Blender

Convert Skewer JSON scenes back to Blender `.blend` files.

### Usage

```bash
<path-to-blender>/blender --background --python scripts/blender/skewer_to_blend/convert.py -- <scene.json> [output.blend]
```

**Example:**

```bash
/Applications/Blender.app/Contents/MacOS/Blender \
  --background \
  --python scripts/blender/skewer_to_blend/convert.py \
  -- data/scenes/panda-0001.json
```

This creates `panda-0001.blend` in the current directory.

---

## See Also

- [Scene Format](scene-format.md) - Skewer JSON format
