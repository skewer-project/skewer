"""
skewer_to_blend/convert.py — Skewer scene → Blender .blend converter
====================================================================

Usage:
    <path-to-blender>/blender --background --python convert.py -- <scene.json> [output.blend]

Loads a Skewer scene.json manifest (or legacy single-file scene) and
writes a Blender .blend file with camera, materials, and geometry.

Supports both:
  - New manifest format: scene.json { camera, layers: [...], context: [...] }
  - Legacy flat format:  scene.json { camera, materials: {...}, objects: [...] }
"""

import sys
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import bpy  # type: ignore
import mathutils  # type: ignore


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


def scale_to_blender(v):
    """Convert a Skewer scale vector to Blender axis order."""
    return (v[0], v[2], v[1])


def rotation_to_blender_euler(rotate):
    """Convert Skewer Y-X-Z Euler degrees to the Blender rotation used by this importer."""
    return mathutils.Euler(
        (
            math.radians(rotate[0]),
            math.radians(-rotate[2]),
            math.radians(rotate[1]),
        ),
        "ZXY",
    )


# ---------------------------------------------------------------------------
# Transform parsing and animation
# ---------------------------------------------------------------------------


@dataclass
class TransformState:
    translate: list[float]
    rotate: list[float]
    scale: list[float]
    time: Optional[float] = None
    curve: object = "linear"


@dataclass
class Timeline:
    fps: float
    start_time: float
    end_time: float
    static_time: float

    def frame_for_time(self, time):
        return 1.0 + (time - self.start_time) * self.fps


def normalize_vec3(value, default):
    if value is None:
        return list(default)
    if isinstance(value, (int, float)):
        scalar = float(value)
        return [scalar, scalar, scalar]
    if len(value) != 3:
        raise ValueError(f"Expected a 3-vector or scalar, got: {value}")
    return [float(value[0]), float(value[1]), float(value[2])]


def patch_transform_state(state, fields):
    translate = state.translate
    rotate = state.rotate
    scale = state.scale

    if "translate" in fields:
        translate = normalize_vec3(fields["translate"], translate)
    if "rotate" in fields:
        rotate = normalize_vec3(fields["rotate"], rotate)
    if "scale" in fields:
        scale = normalize_vec3(fields["scale"], scale)

    return TransformState(translate=translate, rotate=rotate, scale=scale)


def static_transform_state(transform):
    state = TransformState(
        translate=[0.0, 0.0, 0.0],
        rotate=[0.0, 0.0, 0.0],
        scale=[1.0, 1.0, 1.0],
    )
    return patch_transform_state(state, transform or {})


def expanded_keyframes(transform):
    """Expand Skewer patch-style keyframes into full TRS states."""
    if not transform or "keyframes" not in transform:
        return []

    current = TransformState(
        translate=[0.0, 0.0, 0.0],
        rotate=[0.0, 0.0, 0.0],
        scale=[1.0, 1.0, 1.0],
    )
    frames = []

    for kf in transform["keyframes"]:
        current = patch_transform_state(current, kf)
        frames.append(
            TransformState(
                translate=list(current.translate),
                rotate=list(current.rotate),
                scale=list(current.scale),
                time=float(kf["time"]),
                curve=kf.get("curve", "linear"),
            )
        )

    frames.sort(key=lambda k: k.time)
    return frames


def cubic_bezier_axis(p0, p1, p2, p3, t):
    omt = 1.0 - t
    return (
        (omt * omt * omt * p0)
        + (3.0 * omt * omt * t * p1)
        + (3.0 * omt * t * t * p2)
        + (t * t * t * p3)
    )


def cubic_bezier_alpha(p1x, p1y, p2x, p2y, x):
    """Evaluate a CSS-style cubic bezier y value at x using bisection."""
    lo = 0.0
    hi = 1.0
    t = x
    for _ in range(16):
        t = (lo + hi) * 0.5
        sample_x = cubic_bezier_axis(0.0, p1x, p2x, 1.0, t)
        if sample_x < x:
            lo = t
        else:
            hi = t
    return cubic_bezier_axis(0.0, p1y, p2y, 1.0, t)


def curve_alpha(curve, u):
    if curve == "linear" or curve is None:
        return u
    if curve == "ease-in":
        return cubic_bezier_alpha(0.42, 0.0, 1.0, 1.0, u)
    if curve == "ease-out":
        return cubic_bezier_alpha(0.0, 0.0, 0.58, 1.0, u)
    if curve == "ease-in-out":
        return cubic_bezier_alpha(0.42, 0.0, 0.58, 1.0, u)
    if isinstance(curve, dict) and "bezier" in curve:
        p1x, p1y, p2x, p2y = curve["bezier"]
        return cubic_bezier_alpha(float(p1x), float(p1y), float(p2x), float(p2y), u)
    return u


def lerp_vec3(a, b, alpha):
    return [a[i] + (b[i] - a[i]) * alpha for i in range(3)]


def evaluate_transform(transform, time):
    frames = expanded_keyframes(transform)
    if not frames:
        return static_transform_state(transform)
    if time <= frames[0].time:
        return frames[0]
    if time >= frames[-1].time:
        return frames[-1]

    for i in range(1, len(frames)):
        prev = frames[i - 1]
        cur = frames[i]
        if time <= cur.time:
            span = cur.time - prev.time
            if span <= 1e-20:
                return cur
            u = max(0.0, min(1.0, (time - prev.time) / span))
            alpha = curve_alpha(cur.curve, u)
            return TransformState(
                translate=lerp_vec3(prev.translate, cur.translate, alpha),
                rotate=lerp_vec3(prev.rotate, cur.rotate, alpha),
                scale=lerp_vec3(prev.scale, cur.scale, alpha),
                time=time,
                curve=cur.curve,
            )

    return frames[-1]


def apply_transform_state(obj, state):
    obj.location = to_blender(state.translate)
    obj.rotation_euler = rotation_to_blender_euler(state.rotate)
    obj.scale = scale_to_blender(state.scale)


def blender_interpolation_for_curve(curve):
    if curve == "linear" or curve is None:
        return "LINEAR"
    return "BEZIER"


def iter_action_fcurves(action):
    if hasattr(action, "fcurves"):
        yield from action.fcurves
        return

    for layer in getattr(action, "layers", []):
        for strip in getattr(layer, "strips", []):
            for channelbag in getattr(strip, "channelbags", []):
                yield from getattr(channelbag, "fcurves", [])


def apply_animation_interpolation(obj, frames):
    if not obj.animation_data or not obj.animation_data.action:
        return

    for fcurve in iter_action_fcurves(obj.animation_data.action):
        points = fcurve.keyframe_points
        for i, point in enumerate(points):
            if i + 1 < len(frames):
                # Skewer's renderer applies curve data from the destination keyframe.
                point.interpolation = blender_interpolation_for_curve(frames[i + 1].curve)
            else:
                point.interpolation = blender_interpolation_for_curve(frames[i].curve)


def apply_node_transform(obj, transform, timeline):
    base = evaluate_transform(transform, timeline.static_time)
    apply_transform_state(obj, base)

    frames = expanded_keyframes(transform)
    if not frames:
        return

    for frame_state in frames:
        apply_transform_state(obj, frame_state)
        frame = timeline.frame_for_time(frame_state.time)
        obj.keyframe_insert(data_path="location", frame=frame)
        obj.keyframe_insert(data_path="rotation_euler", frame=frame)
        obj.keyframe_insert(data_path="scale", frame=frame)

    apply_animation_interpolation(obj, frames)
    apply_transform_state(obj, base)


def create_empty(name, parent, transform, timeline):
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = "PLAIN_AXES"
    obj.empty_display_size = 0.5
    bpy.context.collection.objects.link(obj)
    obj.parent = parent
    apply_node_transform(obj, transform or {}, timeline)
    return obj


def parent_objects(objects, parent):
    if parent is None:
        return
    for obj in objects:
        obj.parent = parent


def collect_keyframe_times_from_node(node, times):
    transform = node.get("transform", {})
    for keyframe in transform.get("keyframes", []):
        times.append(float(keyframe["time"]))
    for child in node.get("children", []):
        collect_keyframe_times_from_node(child, times)


def collect_keyframe_times(data_blocks):
    times = []
    for data in data_blocks:
        for node in data.get("graph", []):
            collect_keyframe_times_from_node(node, times)
        for node in data.get("objects", []):
            collect_keyframe_times_from_node(node, times)
    return times


def configure_timeline(scene_data, data_blocks):
    key_times = collect_keyframe_times(data_blocks)
    animation = scene_data.get("animation")

    if animation:
        fps = float(animation.get("fps", 24.0))
        start_time = float(animation["start"])
        end_time = float(animation["end"])
        static_time = start_time
    elif key_times:
        fps = 24.0
        start_time = min(key_times)
        end_time = max(key_times)
        static_time = float(scene_data.get("camera", {}).get("shutter_open", start_time))
    else:
        fps = 24.0
        start_time = 0.0
        end_time = 0.0
        static_time = float(scene_data.get("camera", {}).get("shutter_open", 0.0))

    timeline = Timeline(
        fps=max(fps, 1.0),
        start_time=start_time,
        end_time=max(end_time, start_time),
        static_time=static_time,
    )

    scene = bpy.context.scene
    scene.render.fps = max(1, int(round(timeline.fps)))
    scene.frame_start = 1
    duration_frames = math.ceil((timeline.end_time - timeline.start_time) * timeline.fps)
    scene.frame_end = max(1, int(duration_frames) + 1)
    scene.frame_set(scene.frame_start)
    return timeline


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
    scene.cycles.samples = render_data.get("samples_per_pixel",
                                           render_data.get("max_samples", 128))
    scene.cycles.max_bounces = render_data.get("max_depth", 8)

    img = render_data.get("image", {})
    scene.render.resolution_x = img.get("width", 800)
    scene.render.resolution_y = img.get("height", 600)
    scene.render.film_transparent = False

    outfile = img.get("exrfile", img.get("outfile", "render"))
    scene.render.filepath = str(Path(outfile).stem)

    # Black background (matches Skewer's default)
    world = scene.world or bpy.data.worlds.new("World")
    scene.world = world
    world.use_nodes = True
    bg_node = world.node_tree.nodes.get("Background")
    if bg_node:
        bg_node.inputs["Color"].default_value = (0.0, 0.0, 0.0, 1.0)
        bg_node.inputs["Strength"].default_value = 0.0


# ---------------------------------------------------------------------------
# Graph node import
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
    return [obj]


def add_quad(obj_def, materials, idx):
    """Add a single quad mesh for a Skewer quad primitive."""
    verts = [to_blender(v) for v in obj_def["vertices"]]
    mesh = bpy.data.meshes.new(f"Quad_{idx}")
    mesh.from_pydata(verts, [], [(0, 1, 2, 3)])
    mesh.update()
    blender_obj = bpy.data.objects.new(f"Quad_{idx}", mesh)
    bpy.context.collection.objects.link(blender_obj)
    blender_obj.data.materials.append(materials[obj_def["material"]])
    return [blender_obj]


def add_obj(obj_def, materials, scene_dir):
    """Import a Wavefront OBJ file and apply Skewer material/local normalization."""
    obj_path = (scene_dir / obj_def["file"]).resolve()

    bpy.ops.object.select_all(action="DESELECT")

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
        return []

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
        bpy.ops.object.select_all(action="DESELECT")
        for o in imported_objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = imported_objs[0]
        bpy.ops.object.transform_apply(location=True, scale=True)

    # -- Override material -----------------------------------------------
    mat = materials[obj_def["material"]]
    for o in imported_objs:
        if o.type == "MESH":
            o.data.materials.clear()
            o.data.materials.append(mat)

    return imported_objs


def add_leaf_objects(node, materials, scene_dir, idx_counter):
    otype = node.get("type")
    if otype == "sphere":
        return add_sphere(node, materials)
    if otype == "quad":
        objects = add_quad(node, materials, idx_counter[0])
        idx_counter[0] += 1
        return objects
    if otype == "obj":
        return add_obj(node, materials, scene_dir)

    print(f"Warning: unknown graph node type '{otype}', skipping.")
    return []


def process_graph_node(node, materials, scene_dir, idx_counter, parent, timeline):
    """Process a single graph node.  Groups recurse; leaf nodes create objects."""
    if "children" in node:
        group_name = node.get("name", "Group")
        group_obj = create_empty(group_name, parent, node.get("transform", {}), timeline)
        for child in node["children"]:
            process_graph_node(child, materials, scene_dir, idx_counter, group_obj, timeline)
        return

    leaf_parent = parent
    if "transform" in node:
        leaf_name = node.get("name", node.get("type", "Leaf"))
        leaf_parent = create_empty(leaf_name, parent, node["transform"], timeline)

    objects = add_leaf_objects(node, materials, scene_dir, idx_counter)
    parent_objects(objects, leaf_parent)


def load_materials_from_data(data, materials, context, scene_dir):
    """Collect material definitions from a layer/context data dict."""
    for name, mdef in data.get("materials", {}).items():
        if name not in materials:
            materials[name] = create_material(name, mdef)


def load_graph_from_data(data, materials, scene_dir, idx_counter, timeline):
    """Process graph array from a layer/context data dict."""
    for node in data.get("graph", []):
        process_graph_node(node, materials, scene_dir, idx_counter, None, timeline)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main():
    scene_path, output_path = parse_args()
    data = json.loads(scene_path.read_text())

    clear_scene()

    materials = {}
    scene_dir = scene_path.parent
    idx_counter = [0]

    if "layers" in data:
        # ---- New manifest format ----
        setup_camera(data["camera"], data.get("render", {}))

        ctx_blocks = []
        layer_blocks = []

        # Load context files first (shared materials + geometry)
        for ctx_rel in data.get("context", []):
            ctx_path = (scene_dir / ctx_rel).resolve()
            ctx_data = json.loads(ctx_path.read_text())
            ctx_blocks.append(ctx_data)

        # Load layer files
        for layer_rel in data.get("layers", []):
            layer_path = (scene_dir / layer_rel).resolve()
            layer_data = json.loads(layer_path.read_text())
            layer_blocks.append(layer_data)

        timeline = configure_timeline(data, ctx_blocks + layer_blocks)

        for ctx_data in ctx_blocks:
            load_materials_from_data(ctx_data, materials, None, scene_dir)
            load_graph_from_data(ctx_data, materials, scene_dir, idx_counter, timeline)

        for layer_data in layer_blocks:
            load_materials_from_data(layer_data, materials, None, scene_dir)
            load_graph_from_data(layer_data, materials, scene_dir, idx_counter, timeline)

    else:
        # ---- Legacy flat format ----
        setup_camera(data["camera"], data.get("render", {}))
        setup_render(data.get("render", {}))
        timeline = configure_timeline(data, [data])

        for name, mdef in data.get("materials", {}).items():
            if name not in materials:
                materials[name] = create_material(name, mdef)

        for obj_def in data.get("objects", []):
            process_graph_node(obj_def, materials, scene_dir, idx_counter, None, timeline)

    bpy.context.scene.frame_set(bpy.context.scene.frame_start)
    bpy.ops.wm.save_as_mainfile(filepath=str(output_path))
    print(f"Saved: {output_path}")


if __name__ == "__main__":
    main()
