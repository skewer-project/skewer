// Scene parser — pure functions, no React dependency.
// Mirrors the parsing logic in skewer/src/io/scene_loader.cc.

import type {
	AnimatedTransform,
	Camera,
	DielectricMaterial,
	ImageConfig,
	InterpCurve,
	Keyframe,
	LambertianMaterial,
	LayerData,
	Material,
	Medium,
	MetalMaterial,
	NanoVDBMedium,
	NodeTransform,
	ObjNode,
	QuadNode,
	RenderConfig,
	ResolvedLayer,
	ResolvedScene,
	SceneManifest,
	SceneNode,
	SphereNode,
	StaticTransform,
	Vec3,
} from "../types/scene";
import { getNanoVDBBounds } from "./nanovdb-parser";
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
		shutter_open:
			json.shutter_open !== undefined
				? num(json.shutter_open, "camera.shutter_open", 0)
				: undefined,
		shutter_close:
			json.shutter_close !== undefined
				? num(json.shutter_close, "camera.shutter_close", 0)
				: undefined,
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

// --- Media ---

export function parseMedium(name: string, json: unknown): Medium {
	if (!isObject(json)) throw new Error(`medium "${name}": expected object`);
	const type = str(json.type, `medium "${name}".type`);

	switch (type) {
		case "nanovdb":
			return {
				type: "nanovdb",
				sigma_a: parseVec3OrDefault(json.sigma_a, `medium "${name}".sigma_a`, [
					0, 0, 0,
				]),
				sigma_s: parseVec3OrDefault(json.sigma_s, `medium "${name}".sigma_s`, [
					0, 0, 0,
				]),
				g: num(json.g, `medium "${name}".g`, 0),
				density_multiplier: num(
					json.density_multiplier,
					`medium "${name}".density_multiplier`,
					1,
				),
				scale:
					json.scale !== undefined
						? num(json.scale, `medium "${name}".scale`)
						: undefined,
				translate:
					json.translate !== undefined
						? parseVec3(json.translate, `medium "${name}".translate`)
						: undefined,
				file: str(json.file, `medium "${name}".file`),
			} satisfies NanoVDBMedium;
		default:
			throw new Error(`medium "${name}": unknown type "${type}"`);
	}
}

// --- Transforms ---

function parseInterpCurve(v: unknown, field: string): InterpCurve {
	if (typeof v === "string") {
		if (
			v === "linear" ||
			v === "ease-in" ||
			v === "ease-out" ||
			v === "ease-in-out"
		) {
			return v;
		}
		throw new Error(`${field}: unknown interpolation preset "${v}"`);
	}
	if (!isObject(v))
		throw new Error(`${field}: expected string or bezier object`);
	if (v.bezier !== undefined) {
		if (!Array.isArray(v.bezier) || v.bezier.length !== 4) {
			throw new Error(`${field}.bezier: expected [x1,y1,x2,y2]`);
		}
		const b = v.bezier.map((x, i) => {
			if (typeof x !== "number")
				throw new Error(`${field}.bezier[${i}]: expected number`);
			return x;
		});
		return { bezier: b as [number, number, number, number] };
	}
	throw new Error(
		`${field}: expected interpolation string or { bezier: [...] }`,
	);
}

function parseKeyframe(json: unknown, field: string): Keyframe {
	if (!isObject(json)) throw new Error(`${field}: expected object`);
	const k: Keyframe = {
		time: num(json.time, `${field}.time`),
	};
	if (json.translate !== undefined)
		k.translate = parseVec3(json.translate, `${field}.translate`);
	if (json.rotate !== undefined)
		k.rotate = parseVec3(json.rotate, `${field}.rotate`);
	if (json.scale !== undefined) {
		if (typeof json.scale === "number") k.scale = json.scale;
		else k.scale = parseVec3(json.scale, `${field}.scale`);
	}
	if (json.curve !== undefined)
		k.curve = parseInterpCurve(json.curve, `${field}.curve`);
	return k;
}

export function parseNodeTransform(
	json: unknown,
	field: string,
): NodeTransform {
	if (!isObject(json)) throw new Error(`${field}: expected object`);
	if (json.keyframes !== undefined) {
		if (!Array.isArray(json.keyframes)) {
			throw new Error(`${field}.keyframes: expected array`);
		}
		if (json.keyframes.length < 1) {
			throw new Error(`${field}.keyframes: expected at least one keyframe`);
		}
		const keyframes: Keyframe[] = [];
		for (let i = 0; i < json.keyframes.length; i++) {
			keyframes.push(
				parseKeyframe(json.keyframes[i], `${field}.keyframes[${i}]`),
			);
		}
		return { keyframes } satisfies AnimatedTransform;
	}
	const st: StaticTransform = {};
	if (json.translate !== undefined)
		st.translate = parseVec3(json.translate, `${field}.translate`);
	if (json.rotate !== undefined)
		st.rotate = parseVec3(json.rotate, `${field}.rotate`);
	if (json.scale !== undefined) {
		if (typeof json.scale === "number") st.scale = json.scale;
		else st.scale = parseVec3(json.scale, `${field}.scale`);
	}
	return st;
}

function optNodeTransform(
	json: Record<string, unknown>,
	field: string,
): NodeTransform | undefined {
	if (json.transform === undefined) return undefined;
	return parseNodeTransform(json.transform, `${field}.transform`);
}

function parseSphereLeaf(
	json: Record<string, unknown>,
	field: string,
): SphereNode {
	return {
		kind: "sphere",
		material: str(json.material, `${field}.material`),
		center: parseVec3(json.center, `${field}.center`),
		radius: num(json.radius, `${field}.radius`),
		visible:
			json.visible !== undefined
				? bool(json.visible, `${field}.visible`, true)
				: undefined,
		inside_medium: optStr(json.inside_medium),
		outside_medium: optStr(json.outside_medium),
	};
}

function parseQuadLeaf(json: Record<string, unknown>, field: string): QuadNode {
	if (!Array.isArray(json.vertices) || json.vertices.length !== 4) {
		throw new Error(`${field}.vertices: expected array of 4 points`);
	}
	return {
		kind: "quad",
		material: str(json.material, `${field}.material`),
		vertices: [
			parseVec3(json.vertices[0], `${field}.vertices[0]`),
			parseVec3(json.vertices[1], `${field}.vertices[1]`),
			parseVec3(json.vertices[2], `${field}.vertices[2]`),
			parseVec3(json.vertices[3], `${field}.vertices[3]`),
		],
		visible:
			json.visible !== undefined
				? bool(json.visible, `${field}.visible`, true)
				: undefined,
	};
}

function parseObjLeaf(
	json: Record<string, unknown>,
	field: string,
): Omit<ObjNode, "transform"> {
	return {
		kind: "obj",
		file: str(json.file, `${field}.file`),
		material: optStr(json.material),
		auto_fit:
			json.auto_fit !== undefined
				? bool(json.auto_fit, `${field}.auto_fit`, true)
				: undefined,
		visible:
			json.visible !== undefined
				? bool(json.visible, `${field}.visible`, true)
				: undefined,
	};
}

export function parseGraphNode(json: unknown, field: string): SceneNode {
	if (!isObject(json)) throw new Error(`${field}: expected object`);

	const name = optStr(json.name);
	const transform = optNodeTransform(json, field);

	const attachBase = <N extends SceneNode>(n: N): N => {
		if (name !== undefined) n.name = name;
		if (transform !== undefined) n.transform = transform;
		return n;
	};

	if (json.children !== undefined) {
		if (!Array.isArray(json.children)) {
			throw new Error(`${field}.children: expected array`);
		}
		const children: SceneNode[] = [];
		for (let i = 0; i < json.children.length; i++) {
			children.push(
				parseGraphNode(json.children[i], `${field}.children[${i}]`),
			);
		}
		return attachBase({ kind: "group", children });
	}

	const type = str(json.type, `${field}.type`);
	switch (type) {
		case "sphere":
			return attachBase(parseSphereLeaf(json, field));
		case "quad":
			return attachBase(parseQuadLeaf(json, field));
		case "obj":
			return attachBase({ ...parseObjLeaf(json, field) } as ObjNode);
		default:
			throw new Error(`${field}: unknown node type "${type}"`);
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
	const maxSamples = json.max_samples ?? json.samples_per_pixel;
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

	if (json.objects !== undefined) {
		throw new Error(
			"Layer must use 'graph' format — scene needs migration (found legacy \"objects\" field)",
		);
	}
	if (!Array.isArray(json.graph)) {
		throw new Error("layer.graph: expected array");
	}

	const materials: Record<string, Material> = {};
	if (json.materials !== undefined) {
		if (!isObject(json.materials))
			throw new Error("layer.materials: expected object");
		for (const [name, matJson] of Object.entries(json.materials)) {
			materials[name] = parseMaterial(name, matJson);
		}
	}

	const media: Record<string, Medium> = {};
	if (json.media !== undefined) {
		if (!isObject(json.media)) throw new Error("layer.media: expected object");
		for (const [name, medJson] of Object.entries(json.media)) {
			media[name] = parseMedium(name, medJson);
		}
	}

	const graph: SceneNode[] = [];
	for (let i = 0; i < json.graph.length; i++) {
		graph.push(parseGraphNode(json.graph[i], `graph[${i}]`));
	}

	return {
		materials,
		media,
		graph,
		render:
			json.render !== undefined ? parseRenderConfig(json.render) : undefined,
		visible:
			json.visible !== undefined
				? bool(json.visible, "layer.visible", true)
				: undefined,
	};
}

// --- scene.json manifest ---

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

	const resolvedScene: ResolvedScene = {
		camera: manifest.camera,
		contexts,
		layers,
		output_dir: manifest.output_dir,
	};

	// --- Volumetric Bounding Sphere Synchronization ---
	// Sync spheres with inside_medium to the physical bounds of the .nvdb file.
	const allLayers = [...resolvedScene.contexts, ...resolvedScene.layers];
	for (const layer of allLayers) {
		const media = layer.data.media;
		if (!media) continue;

		const visitNode = async (node: SceneNode) => {
			if (node.kind === "sphere" && node.inside_medium) {
				const med = media[node.inside_medium];
				if (med && med.type === "nanovdb") {
					const bounds = await getNanoVDBBounds(dir, med.file);
					if (bounds) {
						const scale = med.scale ?? 1.0;
						const translate = med.translate ?? [0, 0, 0];
						node.center = [0, 0, 0];
						node.radius = bounds.radius;
						node.transform = { translate: translate, rotate: [0, 0, 0], scale: scale };
					}
				}
			}
			if (node.kind === "group") {
				for (const child of node.children) {
					await visitNode(child);
				}
			}
		};

		for (const node of layer.data.graph) {
			await visitNode(node);
		}
	}

	return resolvedScene;
}
