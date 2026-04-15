// Inverse of scene-parser.ts — serialize ResolvedScene to JSON on disk.

import type {
	Camera,
	InterpCurve,
	Keyframe,
	LayerData,
	Material,
	NodeTransform,
	RenderConfig,
	ResolvedScene,
	SceneNode,
} from "../types/scene";
import { isAnimated } from "../types/scene";
import { writeTextFile } from "./fs";

function jsonLine(value: unknown): string {
	return `${JSON.stringify(value, null, 2)}\n`;
}

function serializeCamera(c: Camera): Record<string, unknown> {
	const o: Record<string, unknown> = {
		look_from: c.look_from,
		look_at: c.look_at,
		vup: c.vup,
		vfov: c.vfov,
		aperture_radius: c.aperture_radius,
		focus_distance: c.focus_distance,
	};
	const so = c.shutter_open ?? 0;
	const sc = c.shutter_close ?? 0;
	if (so !== 0) o.shutter_open = so;
	if (sc !== 0) o.shutter_close = sc;
	return o;
}

function serializeMaterial(m: Material): Record<string, unknown> {
	const o: Record<string, unknown> = {
		type: m.type,
		albedo: m.albedo,
		emission: m.emission,
		opacity: m.opacity,
		visible: m.visible,
	};
	if (m.albedo_texture !== undefined) o.albedo_texture = m.albedo_texture;
	if (m.normal_texture !== undefined) o.normal_texture = m.normal_texture;
	if (m.roughness_texture !== undefined)
		o.roughness_texture = m.roughness_texture;
	if (m.type === "metal") o.roughness = m.roughness;
	if (m.type === "dielectric") {
		o.ior = m.ior;
		o.roughness = m.roughness;
	}
	return o;
}

function serializeInterpCurve(c: InterpCurve): unknown {
	if (typeof c === "string") return c;
	return { bezier: c.bezier };
}

function serializeKeyframe(k: Keyframe): Record<string, unknown> {
	const o: Record<string, unknown> = { time: k.time };
	if (k.translate !== undefined) o.translate = k.translate;
	if (k.rotate !== undefined) o.rotate = k.rotate;
	if (k.scale !== undefined) o.scale = k.scale;
	if (k.curve !== undefined) o.curve = serializeInterpCurve(k.curve);
	return o;
}

function serializeNodeTransform(
	t: NodeTransform | undefined,
): Record<string, unknown> | undefined {
	if (t === undefined) return undefined;
	if (isAnimated(t)) {
		return {
			keyframes: t.keyframes.map(serializeKeyframe),
		};
	}
	const o: Record<string, unknown> = {};
	if (t.translate !== undefined) o.translate = t.translate;
	if (t.rotate !== undefined) o.rotate = t.rotate;
	if (t.scale !== undefined) o.scale = t.scale;
	if (Object.keys(o).length === 0) return undefined;
	return o;
}

function serializeSceneNode(node: SceneNode): Record<string, unknown> {
	const xf = serializeNodeTransform(node.transform);
	const name = node.name;

	const withCommon = (j: Record<string, unknown>): Record<string, unknown> => {
		if (name !== undefined) j.name = name;
		if (xf !== undefined) j.transform = xf;
		return j;
	};

	if (node.kind === "group") {
		const j: Record<string, unknown> = {};
		if (name !== undefined) j.name = name;
		if (xf !== undefined) j.transform = xf;
		j.children = node.children.map(serializeSceneNode);
		return j;
	}

	if (node.kind === "sphere") {
		const j: Record<string, unknown> = {
			type: "sphere",
			material: node.material,
			center: node.center,
			radius: node.radius,
		};
		if (node.visible !== undefined) j.visible = node.visible;
		return withCommon(j);
	}

	if (node.kind === "quad") {
		const j: Record<string, unknown> = {
			type: "quad",
			material: node.material,
			vertices: node.vertices,
		};
		if (node.visible !== undefined) j.visible = node.visible;
		return withCommon(j);
	}

	const j: Record<string, unknown> = {
		type: "obj",
		file: node.file,
	};
	if (node.material !== undefined) j.material = node.material;
	if (node.auto_fit !== undefined) j.auto_fit = node.auto_fit;
	if (node.visible !== undefined) j.visible = node.visible;
	return withCommon(j);
}

function serializeImageConfig(
	image: RenderConfig["image"],
): Record<string, unknown> {
	const j: Record<string, unknown> = {
		width: image.width,
		height: image.height,
	};
	if (image.outfile !== undefined) j.outfile = image.outfile;
	if (image.exrfile !== undefined) j.exrfile = image.exrfile;
	return j;
}

function serializeRenderConfig(r: RenderConfig): Record<string, unknown> {
	const j: Record<string, unknown> = {
		integrator: r.integrator,
		max_samples: r.max_samples,
		max_depth: r.max_depth,
		threads: r.threads,
		image: serializeImageConfig(r.image),
	};
	if (r.min_samples !== undefined) j.min_samples = r.min_samples;
	if (r.tile_size !== undefined) j.tile_size = r.tile_size;
	if (r.noise_threshold !== undefined) j.noise_threshold = r.noise_threshold;
	if (r.adaptive_step !== undefined) j.adaptive_step = r.adaptive_step;
	if (r.enable_deep !== undefined) j.enable_deep = r.enable_deep;
	if (r.transparent_background !== undefined) {
		j.transparent_background = r.transparent_background;
	}
	if (r.visibility_depth !== undefined) j.visibility_depth = r.visibility_depth;
	if (r.save_sample_map !== undefined) j.save_sample_map = r.save_sample_map;
	return j;
}

function serializeLayerData(data: LayerData): Record<string, unknown> {
	const materials: Record<string, unknown> = {};
	for (const [name, mat] of Object.entries(data.materials)) {
		materials[name] = serializeMaterial(mat);
	}
	const o: Record<string, unknown> = {
		materials,
		graph: data.graph.map(serializeSceneNode),
	};
	if (data.render !== undefined) o.render = serializeRenderConfig(data.render);
	if (data.visible !== undefined) o.visible = data.visible;
	return o;
}

function serializeManifest(scene: ResolvedScene): Record<string, unknown> {
	return {
		camera: serializeCamera(scene.camera),
		context: scene.contexts.map((l) => l.path),
		layers: scene.layers.map((l) => l.path),
		output_dir: scene.output_dir,
	};
}

export async function saveScene(
	dir: FileSystemDirectoryHandle,
	scene: ResolvedScene,
): Promise<void> {
	const byPath = new Map<string, LayerData>();
	for (const layer of scene.contexts) {
		byPath.set(layer.path, layer.data);
	}
	for (const layer of scene.layers) {
		byPath.set(layer.path, layer.data);
	}

	for (const [path, data] of byPath) {
		await writeTextFile(dir, path, jsonLine(serializeLayerData(data)));
	}

	await writeTextFile(dir, "scene.json", jsonLine(serializeManifest(scene)));
}
