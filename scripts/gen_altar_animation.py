#!/usr/bin/env python3
"""
gen_altar_animation.py
Generates a 120-frame spinning dodecahedron animation on the altar scene.

Output structure (all written to data/scenes/altar_animation/):
  layer_environment.json          — static altar background (rendered ONCE)
  layer_camera.json               — shared camera context
  layer_dodecahedron_XXXX.json    — per-frame spinning dodecahedron (120 files)
  scene_XXXX.json                 — per-frame master scene file (120 files)
"""

import json
import math
import os

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
NUM_FRAMES   = 120
OUTPUT_DIR   = os.path.join(os.path.dirname(__file__), "../data/scenes/altar_animation")

# Camera — same as dodecahedron_altar_scene.json
CAMERA = {
    "look_from": [0.0, 1.5, 6.0],
    "look_at":   [0.0, 0.5, 0.0],
    "vup":       [0.0, 1.0, 0.0],
    "vfov":      50.0,
}

# Dodecahedron resting position: floating above the altar/ring
DODECAHEDRON_TRANSLATE = [0.0, 1.2, 0.0]
DODECAHEDRON_SCALE     = 0.45

# Full rotations across 120 frames (X completes 1 full turn, Y 2 full turns, Z 0.5)
# This creates a complex tumbling motion that looks visually interesting.
X_ROTATIONS_TOTAL = 1.0   # full rotations over all frames
Y_ROTATIONS_TOTAL = 2.0
Z_ROTATIONS_TOTAL = 0.5

# Render settings for layers
RENDER_SETTINGS = {
    "integrator":      "path_trace",
    "max_samples":     512,
    "min_samples":     16,
    "noise_threshold": 0.05,
    "adaptive_step":   16,
    "max_depth":       12,
    "threads":         0,
    "enable_deep":     True,
    "image": {
        "width":  1920,
        "height": 1080,
    }
}

# ---------------------------------------------------------------------------
# Static altar objects (from dodecahedron_altar_scene.json)
# Note: paths are relative to the scenes/ directory.
# "material" must match the MTL material name (Material.003) so the OBJ
# loader's built-in mtllib is honoured rather than overridden.
# ---------------------------------------------------------------------------
ALTAR_OBJECTS_PREFIX = "objects"
ALTAR_MTL_MATERIAL   = "Material.003"   # matches newmtl in *.mtl files

STATIC_OBJECTS = [
    ("obj1_0.obj",           "Altar table top"),
    ("Brazier_1.obj",        "Brazier"),
    ("Brazier_Stand_2.obj",  "Brazier Stand"),
    ("Ceiling_3.obj",        "Ceiling"),
    ("Cube_4.obj",           "Cube"),
    ("Fire_Wood_5.obj",      "Fire Wood"),
    ("Fire_Wood10_6.obj",    "Fire Wood 10"),
    ("Fire_Wood11_7.obj",    "Fire Wood 11"),
    ("Fire_Wood2_8.obj",     "Fire Wood 2"),
    ("Fire_Wood3_9.obj",     "Fire Wood 3"),
    ("Fire_Wood4_10.obj",    "Fire Wood 4"),
    ("Fire_Wood5_11.obj",    "Fire Wood 5"),
    ("Fire_Wood6_12.obj",    "Fire Wood 6"),
    ("Fire_Wood7_13.obj",    "Fire Wood 7"),
    ("Fire_Wood8_14.obj",    "Fire Wood 8"),
    ("Fire_Wood9_15.obj",    "Fire Wood 9"),
    ("Floor_16.obj",         "Floor"),
    ("Pillar_17.obj",        "Pillar"),
    ("Pillar_2_18.obj",      "Pillar 2"),
    ("Pillar_3_19.obj",      "Pillar 3"),
    ("Pillar_Stand_20.obj",  "Pillar Stand"),
    ("Pillar_Stand_2_21.obj","Pillar Stand 2"),
    ("Pillar_Stand_3_22.obj","Pillar Stand 3"),
    ("Ring_23.obj",          "Ring"),
]

# ---------------------------------------------------------------------------
# Synthetic-primitive materials (fire spheres, ambient dome)
# The static OBJ geometry uses Material.003 from the co-located *.mtl files;
# those are NOT listed here — the OBJ loader picks them up automatically.
# ---------------------------------------------------------------------------
ENVIRONMENT_MATERIALS = {
    # Brazier fire glow — warm orange emissive light sources
    "fire": {
        "type":     "lambertian",
        "albedo":   [0.0, 0.0, 0.0],
        "emission": [4.0, 1.8, 0.4],
    },
    # Ambient fill — very dim sky above the ceiling
    "ambient": {
        "type":     "lambertian",
        "albedo":   [0.0, 0.0, 0.0],
        "emission": [0.08, 0.10, 0.18],
    },
}

# Dodecahedron materials — glowing arcane crystal
DODECAHEDRON_MATERIALS = {
    "dodecahedron_shell": {
        "type":      "dielectric",
        "albedo":    [0.85, 0.88, 0.95],
        "ior":       1.52,
    },
    "dodecahedron_glow": {
        "type":     "lambertian",
        "albedo":   [0.0, 0.0, 0.0],
        "emission": [0.6, 0.3, 1.5],
    },
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def write_json(path: str, data: dict) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
    print(f"  wrote {os.path.relpath(path)}")


def rotation_euler_xyz(frame: int, total_frames: int) -> list[float]:
    """Return [rx, ry, rz] Euler angles in degrees for this frame."""
    t = frame / total_frames  # 0.0 → 1.0
    rx = t * X_ROTATIONS_TOTAL * 360.0
    ry = t * Y_ROTATIONS_TOTAL * 360.0
    rz = t * Z_ROTATIONS_TOTAL * 360.0
    return [round(rx, 4), round(ry, 4), round(rz, 4)]


# ---------------------------------------------------------------------------
# Layer generators
# ---------------------------------------------------------------------------

def make_layer_camera() -> dict:
    """Context layer — just camera, no geometry."""
    return {
        "camera": CAMERA,
    }


def make_layer_environment() -> dict:
    """Static background layer with altar geometry and atmospheric lighting."""
    objects = []

    # Emissive fire spheres at brazier positions (approximate)
    # These act as the primary warm light sources for the whole scene
    fire_lights = [
        [-1.8, 0.85, -0.6],
        [ 1.8, 0.85, -0.6],
        [-1.8, 0.85,  0.6],
        [ 1.8, 0.85,  0.6],
    ]
    for pos in fire_lights:
        objects.append({
            "type":     "sphere",
            "material": "fire",
            "center":   pos,
            "radius":   0.08,
        })

    # Large ambient dome above — very dim cool fill light
    objects.append({
        "type":     "sphere",
        "material": "ambient",
        "center":   [0.0, 0.0, 0.0],
        "radius":   80.0,
        "visible":  False,
    })

    # All static altar geometry — material comes from the co-located .mtl file
    # (each OBJ has `mtllib X.mtl` → `usemtl Material.003`).  We pass
    # ALTAR_MTL_MATERIAL so the scene loader knows the expected material name.
    for (obj_file, comment) in STATIC_OBJECTS:
        objects.append({
            "type":      "obj",
            "file":      f"../objects/{obj_file}",
            "material":  ALTAR_MTL_MATERIAL,
            "auto_fit":  False,
            "comment":   comment,
        })

    return {
        "render":    RENDER_SETTINGS,
        "materials": ENVIRONMENT_MATERIALS,
        "objects":   objects,
    }


def make_layer_dodecahedron(frame: int) -> dict:
    """Per-frame layer containing just the spinning dodecahedron."""
    rx, ry, rz = rotation_euler_xyz(frame, NUM_FRAMES)

    return {
        "render":    RENDER_SETTINGS,
        "materials": DODECAHEDRON_MATERIALS,
        "objects": [
            {
                "type":     "obj",
                # Path is relative to data/scenes/altar_animation/
                "file":     "../../objects/dodecahedron_source/dodecahedron.obj",
                "material": "dodecahedron_shell",
                "auto_fit": True,
                "transform": {
                    "translate": DODECAHEDRON_TRANSLATE,
                    "scale":     DODECAHEDRON_SCALE,
                    "rotate":    [rx, ry, rz],
                },
                "comment": f"Dodecahedron frame {frame:04d}",
            },
            # Small inner glow sphere — makes it look like it has an energy core
            {
                "type":     "sphere",
                "material": "dodecahedron_glow",
                "center":   DODECAHEDRON_TRANSLATE,
                "radius":   0.12,
            },
        ],
    }


def make_scene(frame: int) -> dict:
    """Master scene file referencing all three layers."""
    return {
        "camera": CAMERA,
        "layers": [
            "layer_camera.json",
            "layer_environment.json",
            f"layer_dodecahedron_{frame:04d}.json",
        ],
        "output_dir": f"images/altar_animation/frame_{frame:04d}",
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print(f"Generating {NUM_FRAMES}-frame altar animation → {OUTPUT_DIR}/")

    # 1. Static camera context layer (once)
    write_json(
        os.path.join(OUTPUT_DIR, "layer_camera.json"),
        make_layer_camera(),
    )

    # 2. Static environment layer (once)
    write_json(
        os.path.join(OUTPUT_DIR, "layer_environment.json"),
        make_layer_environment(),
    )

    # 3. Per-frame dodecahedron layers + master scene files
    for frame in range(1, NUM_FRAMES + 1):
        write_json(
            os.path.join(OUTPUT_DIR, f"layer_dodecahedron_{frame:04d}.json"),
            make_layer_dodecahedron(frame),
        )
        write_json(
            os.path.join(OUTPUT_DIR, f"scene_{frame:04d}.json"),
            make_scene(frame),
        )

    print(f"\nDone. Generated:")
    print(f"  1  layer_camera.json")
    print(f"  1  layer_environment.json")
    print(f"  {NUM_FRAMES}  layer_dodecahedron_XXXX.json")
    print(f"  {NUM_FRAMES}  scene_XXXX.json")
    print(f"  ─────────────────")
    print(f"  {2 + NUM_FRAMES * 2}  total files")
    print(f"\nTo render with Skewer:")
    print(f"  ./skewer-cli render \\")
    print(f"    --scene 'gs://your-bucket/scenes/altar_animation/scene_####.json' \\")
    print(f"    --frames {NUM_FRAMES} --samples 512")


if __name__ == "__main__":
    main()
