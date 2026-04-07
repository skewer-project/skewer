import sys
import json
import math
from pathlib import Path

import bpy
import mathutils


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------


def parse_args():
    """Parse sys.argv after the '--' separator Blender uses for script args."""
    argv = sys.argv
    try:
        sep = argv.index("--")
        script_args = argv[sep + 1 :]
    except ValueError:
        script_args = []

    if not script_args:
        print(__doc__)
        sys.exit(0)

    scene_path = Path(script_args[0]).resolve()
    if not scene_path.exists():
        print(f"Error: scene file not found: {scene_path}")
        sys.exit(1)

    if len(script_args) >= 2:
        output_path = Path(script_args[1]).resolve()
    else:
        output_path = Path.cwd() / (scene_path.stem + ".blend")

    return scene_path, output_path


# ---------------------------------------------------------------------------
# Coordinate system conversion
# ---------------------------------------------------------------------------


def to_blender(v):
    """Convert a Skewer (Y-up, Z-backward) coord to Blender (Z-up, Y-forward).

    Mapping: (xs, ys, zs) → (xs, -zs, ys)
    This is a proper rotation (det = 1), so face winding and normals are preserved.
    """
    return (v[0], -v[2], v[1])


# ---------------------------------------------------------------------------
# Scene setup
# ---------------------------------------------------------------------------


def clear_scene():
    """Remove all default objects (Cube, Camera, Light)."""
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------


def create_material(name, mat_def):
    """Create a Blender material from a Skewer material definition.

    Supports: lambertian, metal, dielectric (with optional emission on any type).
    Handles Blender 3.x / 4.x API differences via try/except.
    """
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    nodes.clear()

    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    mat.node_tree.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    mtype = mat_def.get("type", "lambertian")
    albedo = mat_def.get("albedo", [1.0, 1.0, 1.0])
    albedo_rgba = (*albedo, 1.0)

    if mtype == "lambertian":
        bsdf.inputs["Base Color"].default_value = albedo_rgba
        bsdf.inputs["Roughness"].default_value = 1.0
        bsdf.inputs["Metallic"].default_value = 0.0
        # Disable specular (4.x uses 'Specular IOR Level', 3.x uses 'Specular')
        try:
            bsdf.inputs["Specular IOR Level"].default_value = 0.0
        except KeyError:
            try:
                bsdf.inputs["Specular"].default_value = 0.0
            except KeyError:
                pass

    elif mtype == "metal":
        bsdf.inputs["Base Color"].default_value = albedo_rgba
        bsdf.inputs["Metallic"].default_value = 1.0
        bsdf.inputs["Roughness"].default_value = mat_def.get("roughness", 0.0)

    elif mtype == "dielectric":
        bsdf.inputs["Base Color"].default_value = albedo_rgba
        bsdf.inputs["IOR"].default_value = mat_def.get("ior", 1.5)
        bsdf.inputs["Roughness"].default_value = 0.0
        # Transmission: 4.x uses 'Transmission Weight', 3.x uses 'Transmission'
        try:
            bsdf.inputs["Transmission Weight"].default_value = 1.0
        except KeyError:
            try:
                bsdf.inputs["Transmission"].default_value = 1.0
            except KeyError:
                pass
        mat.blend_method = "HASHED"

    # Emission — supported on any material type
    emission = mat_def.get("emission")
    if emission:
        strength = max(emission)
        if strength > 0:
            color = [c / strength for c in emission]
        else:
            color = emission
        color_rgba = (*color, 1.0)
        # 4.x uses 'Emission Color' + 'Emission Strength', 3.x uses 'Emission'
        try:
            bsdf.inputs["Emission Color"].default_value = color_rgba
            bsdf.inputs["Emission Strength"].default_value = strength
        except KeyError:
            try:
                bsdf.inputs["Emission"].default_value = color_rgba
                bsdf.inputs["Emission Strength"].default_value = strength
            except KeyError:
                pass

    return mat


# ---------------------------------------------------------------------------
# Camera
# ---------------------------------------------------------------------------


def setup_camera(cam_data, render_data):
    """Add and orient a camera matching the Skewer camera definition."""
    look_from = to_blender(cam_data["look_from"])
    look_at = to_blender(cam_data["look_at"])
    vup = to_blender(cam_data["vup"])

    bpy.ops.object.camera_add(location=look_from)
    cam_obj = bpy.context.object
    bpy.context.scene.camera = cam_obj

    # Build world matrix from look-at vectors.
    # Blender camera convention: -Z is forward, Y is up, X is right.
    forward = (mathutils.Vector(look_at) - mathutils.Vector(look_from)).normalized()
    right = forward.cross(mathutils.Vector(vup)).normalized()
    up = right.cross(forward)

    mat = mathutils.Matrix(
        [
            [right.x, up.x, -forward.x, look_from[0]],
            [right.y, up.y, -forward.y, look_from[1]],
            [right.z, up.z, -forward.z, look_from[2]],
            [0, 0, 0, 1],
        ]
    )
    cam_obj.matrix_world = mat

    # Vertical FOV
    vfov_rad = math.radians(cam_data["vfov"])
    cam_obj.data.sensor_fit = "VERTICAL"
    cam_obj.data.angle = vfov_rad


# ---------------------------------------------------------------------------
# Render settings
# ---------------------------------------------------------------------------


def setup_render(render_data):
    """Configure Cycles render settings from Skewer render block."""
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = render_data.get("samples_per_pixel", 128)
    scene.cycles.max_bounces = render_data.get("max_depth", 8)

    img = render_data.get("image", {})
    scene.render.resolution_x = img.get("width", 800)
    scene.render.resolution_y = img.get("height", 600)
    scene.render.film_transparent = False

    outfile = img.get("exrfile", img.get("outfile", "render"))
    scene.render.filepath = str(Path(outfile).stem)

    # Match Skewer's convention: rays that miss all geometry return black.
    world = scene.world or bpy.data.worlds.new("World")
    scene.world = world
    world.use_nodes = True
    bg_node = world.node_tree.nodes.get("Background")
    if bg_node:
        bg_node.inputs["Color"].default_value = (0.0, 0.0, 0.0, 1.0)
        bg_node.inputs["Strength"].default_value = 0.0


# ---------------------------------------------------------------------------
# Objects
# ---------------------------------------------------------------------------


def add_sphere(obj_def, materials):
    """Add a UV sphere for a Skewer sphere primitive."""
    center = to_blender(obj_def["center"])
    radius = obj_def["radius"]
    bpy.ops.mesh.primitive_uv_sphere_add(
        radius=radius,
        location=center,
        segments=64,
        ring_count=32,
    )
    obj = bpy.context.object
    obj.data.materials.append(materials[obj_def["material"]])


def add_quad(obj_def, materials, idx):
    """Add a single quad mesh for a Skewer quad primitive."""
    verts = [to_blender(v) for v in obj_def["vertices"]]
    mesh = bpy.data.meshes.new(f"Quad_{idx}")
    mesh.from_pydata(verts, [], [(0, 1, 2, 3)])
    mesh.update()
    blender_obj = bpy.data.objects.new(f"Quad_{idx}", mesh)
    bpy.context.collection.objects.link(blender_obj)
    blender_obj.data.materials.append(materials[obj_def["material"]])


def add_obj(obj_def, materials, scene_dir):
    """Import a Wavefront OBJ file and apply Skewer transforms + material."""
    obj_path = (scene_dir / obj_def["file"]).resolve()

    # Deselect everything so we can identify newly imported objects.
    bpy.ops.object.select_all(action="DESELECT")

    # Import OBJ — tell Blender the source is Y-up so it converts to Z-up.
    if bpy.app.version >= (4, 0, 0):
        bpy.ops.wm.obj_import(
            filepath=str(obj_path),
            up_axis="Y",
            forward_axis="NEGATIVE_Z",
        )
    else:
        bpy.ops.import_scene.obj(
            filepath=str(obj_path),
            axis_up="Y",
            axis_forward="-Z",
        )

    imported_objs = list(bpy.context.selected_objects)

    if not imported_objs:
        print(f"Warning: OBJ import produced no objects for {obj_path}")
        return

    # -- auto_fit normalization ------------------------------------------
    if obj_def.get("auto_fit", False):
        inf = float("inf")
        min_c = mathutils.Vector((inf, inf, inf))
        max_c = mathutils.Vector((-inf, -inf, -inf))
        for o in imported_objs:
            for corner in o.bound_box:
                wc = o.matrix_world @ mathutils.Vector(corner)
                min_c = mathutils.Vector((min(min_c[i], wc[i]) for i in range(3)))
                max_c = mathutils.Vector((max(max_c[i], wc[i]) for i in range(3)))
        extent = max_c - min_c
        max_dim = max(extent)
        if max_dim > 0:
            norm_scale = 2.0 / max_dim
            centroid = (min_c + max_c) / 2
            for o in imported_objs:
                o.location -= centroid * norm_scale
                o.scale *= norm_scale
        # Apply location + scale so subsequent transforms compose cleanly.
        bpy.ops.object.select_all(action="DESELECT")
        for o in imported_objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = imported_objs[0]
        bpy.ops.object.transform_apply(location=True, scale=True)

    # -- Scene-level transform -------------------------------------------
    # After OBJ import with Y→Z conversion the axis mapping is:
    #   Skewer X → Blender X,  Skewer Y → Blender Z,  Skewer Z → Blender -Y
    tf = obj_def.get("transform", {})
    scale = tf.get("scale", [1.0, 1.0, 1.0])
    rotate = tf.get("rotate", [0.0, 0.0, 0.0])  # [rx, ry, rz] degrees
    translate = tf.get("translate", [0.0, 0.0, 0.0])

    for o in imported_objs:
        # Scale: swap Y/Z components to match coord-system rotation.
        o.scale = (scale[0], scale[2], scale[1])
        # Rotation order Skewer YXZ → Blender ZXY.
        # rx → X (same), ry → Z (same sign), rz → Y (negated).
        o.rotation_euler.order = "ZXY"
        o.rotation_euler = (
            math.radians(rotate[0]),  # rx stays X
            math.radians(-rotate[2]),  # -rz → Blender Y
            math.radians(rotate[1]),  # ry  → Blender Z
        )
        o.location = to_blender(translate)

    # -- Override material -----------------------------------------------
    mat = materials[obj_def["material"]]
    for o in imported_objs:
        if o.type == "MESH":
            o.data.materials.clear()
            o.data.materials.append(mat)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main():
    scene_path, output_path = parse_args()
    data = json.loads(scene_path.read_text())

    clear_scene()

    # Build material lookup dict
    materials = {
        name: create_material(name, mdef)
        for name, mdef in data.get("materials", {}).items()
    }

    # Camera and render settings
    setup_camera(data["camera"], data.get("render", {}))
    setup_render(data.get("render", {}))

    # Objects
    for i, obj_def in enumerate(data.get("objects", [])):
        otype = obj_def.get("type")
        if otype == "sphere":
            add_sphere(obj_def, materials)
        elif otype == "quad":
            add_quad(obj_def, materials, i)
        elif otype == "obj":
            add_obj(obj_def, materials, scene_path.parent)
        else:
            print(f"Warning: unknown object type '{otype}' (object #{i}), skipping.")

    bpy.ops.wm.save_as_mainfile(filepath=str(output_path))
    print(f"Saved: {output_path}")


if __name__ == "__main__":
    main()
