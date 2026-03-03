"""
blender_export.py — Blender → skewer scene.json exporter
=========================================================
Run inside Blender's Scripting workspace, or from the command line:

    blender my_scene.blend --background --python blender_export.py

The script inspects every visible MESH object in the active scene and
writes a scene.json (plus per-object OBJ files) that the skewer renderer
can consume directly.

Object type mapping
-------------------
  Sphere  — object has a custom property  skewer_type = "sphere"
  Quad    — mesh with exactly 4 vertices AND 1 face (after modifiers)
  OBJ     — everything else (triangulated and baked to world-space OBJ)

Coordinate conversion: Blender Z-up → renderer Y-up
----------------------------------------------------
  (x, y, z)  →  (x, z, -y)
"""

import bpy
import json
import math
import os
import sys
from mathutils import Vector

# ---------------------------------------------------------------------------
# CONFIGURATION — edit these before running
# ---------------------------------------------------------------------------

# Absolute path to the directory where scene.json (and subdirs) will be written.
# Defaults to the directory of the currently open .blend file.
_blend_dir = os.path.dirname(bpy.data.filepath) if bpy.data.filepath else "/tmp"
OUTPUT_DIR = _blend_dir

# Base name for the exported scene (no extension).
# Defaults to the .blend filename stem, or "scene" if unsaved.
SCENE_NAME = (
    os.path.splitext(os.path.basename(bpy.data.filepath))[0]
    if bpy.data.filepath
    else "scene"
)

SAMPLES_PER_PIXEL = 200   # skewer SPP (independent of Cycles sample count)
MAX_DEPTH         = 50    # skewer path depth
NUM_THREADS       = 0     # 0 = use all available cores

# ---------------------------------------------------------------------------
# Axis conversion helper
# ---------------------------------------------------------------------------

def to_yup(v):
    """Convert a Blender Z-up Vector/tuple to renderer Y-up coordinates."""
    return [v[0], v[2], -v[1]]


# ---------------------------------------------------------------------------
# Object type detection
# ---------------------------------------------------------------------------

def is_sphere(obj):
    """Return True if the object has been tagged as a skewer sphere."""
    return obj.get("skewer_type", "") == "sphere"


def is_quad(obj):
    """
    Return True if the object's evaluated mesh has exactly 4 vertices and
    1 face — the hallmark of a Blender Plane that has not been subdivided.
    """
    depsgraph = bpy.context.evaluated_depsgraph_get()
    eval_obj  = obj.evaluated_get(depsgraph)
    mesh      = eval_obj.to_mesh()
    try:
        return len(mesh.vertices) == 4 and len(mesh.polygons) == 1
    finally:
        eval_obj.to_mesh_clear()


# ---------------------------------------------------------------------------
# Material conversion
# ---------------------------------------------------------------------------

def _get_principled(mat):
    """Return the first Principled BSDF node in mat, or None."""
    if not mat or not mat.use_nodes:
        return None
    for node in mat.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            return node
    return None


def convert_material(mat):
    """
    Translate a Blender material into a skewer material dict.
    Falls back to a neutral gray Lambertian if no Principled BSDF is found.
    """
    node = _get_principled(mat)

    if node is None:
        return {"type": "lambertian", "albedo": [0.5, 0.5, 0.5]}

    # Helper: read a socket value safely
    def sock(name, default=0.0):
        s = node.inputs.get(name)
        return s.default_value if s else default

    base_color  = sock("Base Color", [0.8, 0.8, 0.8, 1.0])
    albedo      = [round(float(base_color[i]), 6) for i in range(3)]
    metallic    = float(sock("Metallic"))
    roughness   = float(sock("Roughness"))
    transmission = float(sock("Transmission Weight",
                              sock("Transmission")))   # name changed in 4.x
    ior         = float(sock("IOR", 1.45))

    # Emission
    emit_color    = sock("Emission Color", [0.0, 0.0, 0.0, 1.0])
    emit_strength = float(sock("Emission Strength", 0.0))

    # Determine material type
    if transmission > 0.5:
        result = {"type": "dielectric", "albedo": albedo, "ior": round(ior, 4)}
    elif metallic > 0.5:
        result = {
            "type": "metal",
            "albedo": albedo,
            "roughness": round(roughness, 4),
        }
    else:
        result = {"type": "lambertian", "albedo": albedo}

    # Emission (any type can emit)
    if emit_strength > 0.0:
        emission = [
            round(float(emit_color[i]) * emit_strength, 6) for i in range(3)
        ]
        if any(e > 0.0 for e in emission):
            result["emission"] = emission

    return result


# ---------------------------------------------------------------------------
# OBJ export
# ---------------------------------------------------------------------------

def export_obj(obj, objects_dir, idx):
    """
    Export a single mesh object to an OBJ file with world-space coordinates
    already in Y-up orientation.  Smooth shading is forced on all faces so
    that the OBJ exporter writes interpolated per-vertex normals (equivalent
    to Blender's "Shade Smooth"), which eliminates the hard edges that appear
    on curved surfaces when rendered with transmission/glass materials.

    Returns the relative path from OUTPUT_DIR to the written OBJ file.
    """
    safe_name  = "".join(c if c.isalnum() or c in "-_." else "_" for c in obj.name)
    filename   = f"{safe_name}_{idx}.obj"
    obj_path   = os.path.join(objects_dir, filename)
    rel_path   = os.path.join("objects", filename)

    # Deselect all, then select only this object
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    # Force smooth shading on every polygon so the OBJ exporter writes
    # interpolated vertex normals instead of flat face normals.
    # We save and restore the original per-face smooth flags so that the
    # user's Blender scene is not permanently modified.
    original_smooth = [p.use_smooth for p in obj.data.polygons]
    for p in obj.data.polygons:
        p.use_smooth = True

    try:
        kwargs = dict(
            filepath                  = obj_path,
            export_selected_objects   = True,
            apply_modifiers           = True,
            export_triangulated_mesh  = True,
            forward_axis              = "NEGATIVE_Z",
            up_axis                   = "Y",
            export_materials          = True,
            export_normals            = True,
            export_uv                 = True,
        )

        # Blender 3.3+ uses wm.obj_export; older versions use export_scene.obj.
        if hasattr(bpy.ops.wm, "obj_export"):
            bpy.ops.wm.obj_export(**kwargs)
        else:
            # Legacy fallback (pre-3.3)
            legacy_kwargs = dict(
                filepath           = obj_path,
                use_selection      = True,
                use_mesh_modifiers = True,
                use_triangles      = True,
                axis_forward       = "-Z",
                axis_up            = "Y",
            )
            bpy.ops.export_scene.obj(**legacy_kwargs)
    finally:
        # Restore original smooth state regardless of export success/failure
        for p, smooth in zip(obj.data.polygons, original_smooth):
            p.use_smooth = smooth

    return rel_path


# ---------------------------------------------------------------------------
# Per-object processing
# ---------------------------------------------------------------------------

def _mat_name(obj):
    """Return the material name for the first slot, or 'default'."""
    if obj.material_slots and obj.material_slots[0].material:
        return obj.material_slots[0].material.name
    return "default"


def _sphere_entry(obj, mat_name):
    """Build a sphere object dict from a tagged Blender object."""
    loc = obj.matrix_world.translation
    # Radius: use the largest world-space scale component as radius * local scale.
    # For a default Blender sphere (radius=1 in local space), scale gives the radius.
    scale  = obj.matrix_world.to_scale()
    radius = max(abs(scale.x), abs(scale.y), abs(scale.z))
    # Retrieve per-object override radius if set
    if "skewer_radius" in obj:
        radius = float(obj["skewer_radius"])

    return {
        "type"    : "sphere",
        "material": mat_name,
        "center"  : [round(v, 6) for v in to_yup(loc)],
        "radius"  : round(radius, 6),
    }


def _quad_entry(obj, mat_name):
    """
    Build a quad object dict.  Vertices are read from the evaluated mesh
    and converted to Y-up world space.
    """
    depsgraph = bpy.context.evaluated_depsgraph_get()
    eval_obj  = obj.evaluated_get(depsgraph)
    mesh      = eval_obj.to_mesh()
    try:
        mw   = obj.matrix_world
        face = mesh.polygons[0]
        verts = [to_yup(mw @ mesh.vertices[vi].co) for vi in face.vertices]
        # Ensure exactly 4 verts (should be guaranteed by is_quad, but be safe)
        verts = [v for v in verts][:4]
        return {
            "type"    : "quad",
            "material": mat_name,
            "vertices": [[round(c, 6) for c in v] for v in verts],
        }
    finally:
        eval_obj.to_mesh_clear()


def process_objects(scene, out_dir):
    """
    Walk all visible MESH objects and return:
      materials_dict  — {name: skewer_material_dict}
      objects_list    — [skewer_object_dict, ...]
    """
    objects_dir = os.path.join(out_dir, "objects")
    os.makedirs(objects_dir, exist_ok=True)

    materials_dict = {}
    objects_list   = []
    obj_idx        = 0

    for obj in scene.objects:
        # Skip non-mesh, hidden, or render-disabled objects
        if obj.type != "MESH":
            continue
        if obj.hide_render:
            continue
        # Check viewport visibility (respects collections)
        if not obj.visible_get():
            continue

        mat_name = _mat_name(obj)

        # Collect material definition (deduplicated by name)
        if mat_name not in materials_dict:
            mat = (
                obj.material_slots[0].material
                if obj.material_slots and obj.material_slots[0].material
                else None
            )
            if mat is None and mat_name == "default":
                materials_dict["default"] = {
                    "type": "lambertian",
                    "albedo": [0.8, 0.8, 0.8],
                }
            else:
                materials_dict[mat_name] = convert_material(mat)

        # Build the object entry
        if is_sphere(obj):
            entry = _sphere_entry(obj, mat_name)
        elif is_quad(obj):
            entry = _quad_entry(obj, mat_name)
        else:
            rel_path = export_obj(obj, objects_dir, obj_idx)
            obj_idx += 1
            entry = {
                "type"    : "obj",
                "file"    : rel_path,
                "material": mat_name,
                "auto_fit": False,
            }

        # Preserve Blender object name as a comment for readability
        entry["comment"] = obj.name
        objects_list.append(entry)

    return materials_dict, objects_list


# ---------------------------------------------------------------------------
# Camera
# ---------------------------------------------------------------------------

def build_camera(scene):
    """Return a skewer camera dict from the scene's active camera."""
    cam_obj = scene.camera
    if cam_obj is None:
        print("[skewer-export] WARNING: No active camera found; using defaults.")
        return {
            "look_from": [0.0, 1.0, 5.0],
            "look_at"  : [0.0, 0.0, 0.0],
            "vup"      : [0.0, 1.0, 0.0],
            "vfov"     : 50.0,
        }

    cam = cam_obj.data
    mw  = cam_obj.matrix_world

    # Camera local -Z axis points forward in Blender
    fwd_local    = Vector((0.0, 0.0, -1.0))
    fwd_world    = mw.to_3x3() @ fwd_local
    look_from_bl = mw.translation
    look_at_bl   = look_from_bl + fwd_world * 5.0  # arbitrary forward distance

    # Vertical FOV: cam.angle is the horizontal FOV for perspective cameras;
    # cam.angle_y is vertical.  Fall back to deriving from angle + aspect if needed.
    if hasattr(cam, "angle_y") and cam.angle_y > 0:
        vfov = math.degrees(cam.angle_y)
    else:
        aspect = scene.render.resolution_y / max(scene.render.resolution_x, 1)
        vfov   = math.degrees(2.0 * math.atan(math.tan(cam.angle / 2.0) * aspect))

    return {
        "look_from": [round(v, 6) for v in to_yup(look_from_bl)],
        "look_at"  : [round(v, 6) for v in to_yup(look_at_bl)],
        "vup"      : [0.0, 1.0, 0.0],
        "vfov"     : round(vfov, 4),
    }


# ---------------------------------------------------------------------------
# Render settings
# ---------------------------------------------------------------------------

def build_render(scene, scene_name):
    """Return a skewer render dict."""
    r = scene.render
    return {
        "integrator"      : "path_trace",
        "samples_per_pixel": SAMPLES_PER_PIXEL,
        "max_depth"       : MAX_DEPTH,
        "threads"         : NUM_THREADS,
        "image"           : {
            "width"  : r.resolution_x,
            "height" : r.resolution_y,
            "outfile": f"images/{scene_name}.png",
        },
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    scene = bpy.context.scene

    out_dir = OUTPUT_DIR
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(os.path.join(out_dir, "images"), exist_ok=True)

    print(f"[skewer-export] Output directory : {out_dir}")
    print(f"[skewer-export] Scene name       : {SCENE_NAME}")

    # --- Gather objects & materials ---
    materials, objects = process_objects(scene, out_dir)

    # --- Camera ---
    camera = build_camera(scene)

    # --- Render ---
    render = build_render(scene, SCENE_NAME)

    # --- Assemble scene.json ---
    scene_data = {
        "render"   : render,
        "camera"   : camera,
        "materials": materials,
        "objects"  : objects,
    }

    json_path = os.path.join(out_dir, f"{SCENE_NAME}.json")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(scene_data, f, indent=2)

    # --- Summary ---
    print(f"[skewer-export] Written: {json_path}")
    print(f"[skewer-export] Materials : {len(materials)}")
    print(f"[skewer-export] Objects   : {len(objects)}")
    n_spheres = sum(1 for o in objects if o["type"] == "sphere")
    n_quads   = sum(1 for o in objects if o["type"] == "quad")
    n_objs    = sum(1 for o in objects if o["type"] == "obj")
    print(f"[skewer-export]   spheres={n_spheres}  quads={n_quads}  obj={n_objs}")
    print(
        f"[skewer-export] Render with:\n"
        f"  ./build/relwithdebinfo/skewer/skewer-render {json_path}"
    )


main()
