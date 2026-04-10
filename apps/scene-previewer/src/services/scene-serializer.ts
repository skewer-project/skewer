// Inverse of scene-parser.ts — serialize ResolvedScene to JSON on disk.

import type {
	Camera,
	LayerData,
	Material,
	ObjFileObject,
	QuadObject,
	RenderConfig,
	ResolvedScene,
	SceneObject,
	SphereObject,
	Transform,
} from "../types/scene";
import { writeTextFile } from "./fs";

function jsonLine(value: unknown): string {
	return `${JSON.stringify(value, null, 2)}\n`;
}

function serializeCamera(c: Camera): Record<string, unknown> {
	return {
		look_from: c.look_from,
		look_at: c.look_at,
		vup: c.vup,
		vfov: c.vfov,
		aperture_radius: c.aperture_radius,
		focus_distance: c.focus_distance,
	};
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

function serializeTransform(t: Transform): Record<string, unknown> {
	const o: Record<string, unknown> = {};
	if (t.translate !== undefined) o.translate = t.translate;
	if (t.rotate !== undefined) o.rotate = t.rotate;
	if (t.scale !== undefined) o.scale = t.scale;
	return o;
}

function serializeSphere(o: SphereObject): Record<string, unknown> {
	const j: Record<string, unknown> = {
		type: "sphere",
		material: o.material,
		center: o.center,
		radius: o.radius,
	};
	if (o.visible !== undefined) j.visible = o.visible;
	return j;
}

function serializeQuad(o: QuadObject): Record<string, unknown> {
	const j: Record<string, unknown> = {
		type: "quad",
		material: o.material,
		vertices: o.vertices,
	};
	if (o.visible !== undefined) j.visible = o.visible;
	return j;
}

function serializeObj(o: ObjFileObject): Record<string, unknown> {
	const j: Record<string, unknown> = {
		type: "obj",
		file: o.file,
	};
	if (o.material !== undefined) j.material = o.material;
	if (o.auto_fit !== undefined) j.auto_fit = o.auto_fit;
	if (o.visible !== undefined) j.visible = o.visible;
	if (o.transform !== undefined) j.transform = serializeTransform(o.transform);
	return j;
}

function serializeSceneObject(obj: SceneObject): Record<string, unknown> {
	switch (obj.type) {
		case "sphere":
			return serializeSphere(obj);
		case "quad":
			return serializeQuad(obj);
		case "obj":
			return serializeObj(obj);
	}
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
		objects: data.objects.map(serializeSceneObject),
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

/**
 * Writes all layer/context JSON files, then `scene.json`.
 * Last entry wins if the same relative path appears in both lists (should not happen from load).
 */
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
