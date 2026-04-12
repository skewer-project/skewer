// Scene parser — pure functions, no React dependency.
// Mirrors the parsing logic in skewer/src/io/scene_loader.cc.

import type {
	Camera,
	DielectricMaterial,
	ImageConfig,
	LambertianMaterial,
	LayerData,
	Material,
	MetalMaterial,
	ObjFileObject,
	QuadObject,
	RenderConfig,
	ResolvedLayer,
	ResolvedScene,
	SceneManifest,
	SceneObject,
	SphereObject,
	Transform,
	Vec3,
} from "../types/scene";
import { readJsonFile } from "./fs";

// --- Primitive helpers ---

function isObject(v: unknown): v is Record<string, unknown> {
	return typeof v === "object" && v !== null && !Array.isArray(v);
}

function parseVec3(v: unknown, field: string): Vec3 {
	if (!Array.isArray(v) || v.length < 3)
		throw new Error(`${field}: expected [x,y,z] array`);
	const [x, y, z] = v;
	if (typeof x !== "number" || typeof y !== "number" || typeof z !== "number") {
		throw new Error(`${field}: all components must be numbers`);
	}
	return [x, y, z];
}

function parseVec3OrDefault(v: unknown, field: string, def: Vec3): Vec3 {
	return v !== undefined ? parseVec3(v, field) : def;
}

function num(v: unknown, field: string, def?: number): number {
	if (v === undefined && def !== undefined) return def;
	if (typeof v !== "number") throw new Error(`${field}: expected number`);
	return v;
}

function str(v: unknown, field: string): string {
	if (typeof v !== "string") throw new Error(`${field}: expected string`);
	return v;
}

function bool(v: unknown, field: string, def: boolean): boolean {
	if (v === undefined) return def;
	if (typeof v !== "boolean") throw new Error(`${field}: expected boolean`);
	return v;
}

function optStr(v: unknown): string | undefined {
	return typeof v === "string" ? v : undefined;
}

function stemFromPath(path: string): string {
	const filename = path.split("/").pop() ?? path;
	return filename.replace(/\.[^.]+$/, "");
}

// --- Camera ---

export function parseCamera(json: unknown): Camera {
	if (!isObject(json)) throw new Error("camera: expected object");
	return {
		look_from: parseVec3(json.look_from, "camera.look_from"),
		look_at: parseVec3(json.look_at, "camera.look_at"),
		vup: parseVec3OrDefault(json.vup, "camera.vup", [0, 1, 0]),
		vfov: num(json.vfov, "camera.vfov", 90),
		aperture_radius: num(json.aperture_radius, "camera.aperture_radius", 0),
		focus_distance: num(json.focus_distance, "camera.focus_distance", 1),
	};
}

// --- Materials ---

function parseMaterialBase(json: Record<string, unknown>) {
	return {
		albedo: parseVec3OrDefault(json.albedo, "material.albedo", [1, 1, 1]),
		emission: parseVec3OrDefault(json.emission, "material.emission", [0, 0, 0]),
		opacity: parseVec3OrDefault(json.opacity, "material.opacity", [1, 1, 1]),
		visible: bool(json.visible, "material.visible", true),
		albedo_texture: optStr(json.albedo_texture),
		normal_texture: optStr(json.normal_texture),
		roughness_texture: optStr(json.roughness_texture),
	};
}

export function parseMaterial(name: string, json: unknown): Material {
	if (!isObject(json)) throw new Error(`material "${name}": expected object`);
	const type = str(json.type, `material "${name}".type`);
	const base = parseMaterialBase(json);

	switch (type) {
		case "lambertian":
			return { ...base, type: "lambertian" } satisfies LambertianMaterial;
		case "metal":
			return {
				...base,
				type: "metal",
				roughness: num(json.roughness, `material "${name}".roughness`, 0),
			} satisfies MetalMaterial;
		case "dielectric":
			return {
				...base,
				type: "dielectric",
				ior: num(json.ior, `material "${name}".ior`),
				roughness: num(json.roughness, `material "${name}".roughness`, 0),
			} satisfies DielectricMaterial;
		default:
			throw new Error(`material "${name}": unknown type "${type}"`);
	}
}

// --- Objects ---

function parseTransform(json: unknown): Transform {
	if (!isObject(json)) throw new Error("transform: expected object");
	const result: Transform = {};
	if (json.translate !== undefined)
		result.translate = parseVec3(json.translate, "transform.translate");
	if (json.rotate !== undefined)
		result.rotate = parseVec3(json.rotate, "transform.rotate");
	if (json.scale !== undefined) {
		if (typeof json.scale === "number") result.scale = json.scale;
		else result.scale = parseVec3(json.scale, "transform.scale");
	}
	return result;
}

function parseSphere(json: Record<string, unknown>): SphereObject {
	return {
		type: "sphere",
		material: str(json.material, "sphere.material"),
		center: parseVec3(json.center, "sphere.center"),
		radius: num(json.radius, "sphere.radius"),
		visible:
			json.visible !== undefined
				? bool(json.visible, "sphere.visible", true)
				: undefined,
	};
}

function parseQuad(json: Record<string, unknown>): QuadObject {
	if (!Array.isArray(json.vertices) || json.vertices.length !== 4) {
		throw new Error("quad.vertices: expected array of 4 points");
	}
	return {
		type: "quad",
		material: str(json.material, "quad.material"),
		vertices: [
			parseVec3(json.vertices[0], "quad.vertices[0]"),
			parseVec3(json.vertices[1], "quad.vertices[1]"),
			parseVec3(json.vertices[2], "quad.vertices[2]"),
			parseVec3(json.vertices[3], "quad.vertices[3]"),
		],
		visible:
			json.visible !== undefined
				? bool(json.visible, "quad.visible", true)
				: undefined,
	};
}

function parseObjFile(json: Record<string, unknown>): ObjFileObject {
	return {
		type: "obj",
		file: str(json.file, "obj.file"),
		material: optStr(json.material),
		auto_fit:
			json.auto_fit !== undefined
				? bool(json.auto_fit, "obj.auto_fit", true)
				: undefined,
		visible:
			json.visible !== undefined
				? bool(json.visible, "obj.visible", true)
				: undefined,
		transform:
			json.transform !== undefined ? parseTransform(json.transform) : undefined,
	};
}

export function parseObject(json: unknown, index: number): SceneObject {
	if (!isObject(json)) throw new Error(`objects[${index}]: expected object`);
	const type = str(json.type, `objects[${index}].type`);
	switch (type) {
		case "sphere":
			return parseSphere(json);
		case "quad":
			return parseQuad(json);
		case "obj":
			return parseObjFile(json);
		default:
			throw new Error(`objects[${index}]: unknown type "${type}"`);
	}
}

// --- Render config ---

function parseImageConfig(json: unknown): ImageConfig {
	if (!isObject(json)) throw new Error("render.image: expected object");
	return {
		width: num(json.width, "render.image.width", 800),
		height: num(json.height, "render.image.height", 450),
		outfile: optStr(json.outfile),
		exrfile: optStr(json.exrfile),
	};
}

function parseRenderConfig(json: unknown): RenderConfig {
	if (!isObject(json)) throw new Error("render: expected object");
	const maxSamples = json.max_samples ?? json.samples_per_pixel; // legacy alias
	return {
		integrator: json.integrator === "normals" ? "normals" : "path_trace",
		max_samples: num(maxSamples, "render.max_samples", 200),
		min_samples:
			json.min_samples !== undefined
				? num(json.min_samples, "render.min_samples")
				: undefined,
		max_depth: num(json.max_depth, "render.max_depth", 50),
		threads: num(json.threads, "render.threads", 0),
		tile_size:
			json.tile_size !== undefined
				? num(json.tile_size, "render.tile_size")
				: undefined,
		noise_threshold:
			json.noise_threshold !== undefined
				? num(json.noise_threshold, "render.noise_threshold")
				: undefined,
		adaptive_step:
			json.adaptive_step !== undefined
				? num(json.adaptive_step, "render.adaptive_step")
				: undefined,
		enable_deep:
			json.enable_deep !== undefined
				? bool(json.enable_deep, "render.enable_deep", false)
				: undefined,
		transparent_background:
			json.transparent_background !== undefined
				? bool(
						json.transparent_background,
						"render.transparent_background",
						false,
					)
				: undefined,
		visibility_depth:
			json.visibility_depth !== undefined
				? num(json.visibility_depth, "render.visibility_depth")
				: undefined,
		save_sample_map:
			json.save_sample_map !== undefined
				? bool(json.save_sample_map, "render.save_sample_map", false)
				: undefined,
		image: parseImageConfig(json.image ?? {}),
	};
}

// --- Layer / Context ---

export function parseLayerData(json: unknown): LayerData {
	if (!isObject(json)) throw new Error("layer: expected object");

	const materials: Record<string, Material> = {};
	if (json.materials !== undefined) {
		if (!isObject(json.materials))
			throw new Error("layer.materials: expected object");
		for (const [name, matJson] of Object.entries(json.materials)) {
			materials[name] = parseMaterial(name, matJson);
		}
	}

	const objects: SceneObject[] = [];
	if (json.objects !== undefined) {
		if (!Array.isArray(json.objects))
			throw new Error("layer.objects: expected array");
		for (let i = 0; i < json.objects.length; i++) {
			objects.push(parseObject(json.objects[i], i));
		}
	}

	return {
		materials,
		objects,
		render:
			json.render !== undefined ? parseRenderConfig(json.render) : undefined,
		visible:
			json.visible !== undefined
				? bool(json.visible, "layer.visible", true)
				: undefined,
	};
}

// --- scene.json manifest ---

// Parses scene.json manifest and validates required fields.
export function parseSceneManifest(json: unknown): SceneManifest {
	if (!isObject(json)) throw new Error("scene.json: expected object");
	if (!json.camera)
		throw new Error("scene.json: missing required field 'camera'");
	if (!json.layers)
		throw new Error("scene.json: missing required field 'layers'");

	const context = Array.isArray(json.context)
		? json.context.map((p: unknown) => str(p, "context path"))
		: [];
	const layers = (json.layers as unknown[]).map((p: unknown) =>
		str(p, "layer path"),
	);

	return {
		camera: parseCamera(json.camera),
		context,
		layers,
		output_dir: typeof json.output_dir === "string" ? json.output_dir : "",
	};
}

// --- Scene loading orchestrator ---

// Loads scene.json plus layer/context files into a resolved scene.
export async function loadScene(
	dir: FileSystemDirectoryHandle,
): Promise<ResolvedScene> {
	const manifestJson = await readJsonFile(dir, "scene.json");
	const manifest = parseSceneManifest(manifestJson);

	const loadLayer = async (path: string): Promise<ResolvedLayer> => {
		const json = await readJsonFile(dir, path);
		return {
			name: stemFromPath(path),
			path,
			data: parseLayerData(json),
		};
	};

	const [contexts, layers] = await Promise.all([
		Promise.all(manifest.context.map(loadLayer)),
		Promise.all(manifest.layers.map(loadLayer)),
	]);

	return {
		camera: manifest.camera,
		contexts,
		layers,
		output_dir: manifest.output_dir,
	};
}
