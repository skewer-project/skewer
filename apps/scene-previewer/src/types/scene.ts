// TypeScript types mirroring the Skewer scene JSON schema.
// Reference: skewer/src/io/scene_loader.cc

export type Vec3 = [number, number, number];

export type CameraHandle = "look_from" | "look_at";

export type InterpCurve =
	| "linear"
	| "ease-in"
	| "ease-out"
	| "ease-in-out"
	| { bezier: [number, number, number, number] };

// --- Camera ---

export interface CameraKeyframe {
	time: number;
	look_from?: Vec3;
	look_at?: Vec3;
	vup?: Vec3;
	vfov?: number;
	aperture_radius?: number;
	focus_distance?: number;
	curve?: InterpCurve;
}

export interface Camera {
	look_from: Vec3;
	look_at: Vec3;
	vup: Vec3;
	vfov: number;
	aperture_radius: number;
	focus_distance: number;
	/** Motion blur shutter (default 0). */
	shutter_open?: number;
	shutter_close?: number;
	keyframes?: CameraKeyframe[];
}

// --- Materials ---

interface MaterialBase {
	albedo: Vec3;
	emission: Vec3;
	opacity: Vec3;
	visible: boolean;
	albedo_texture?: string;
	normal_texture?: string;
	roughness_texture?: string;
}

export interface LambertianMaterial extends MaterialBase {
	type: "lambertian";
}

export interface MetalMaterial extends MaterialBase {
	type: "metal";
	roughness: number;
}

export interface DielectricMaterial extends MaterialBase {
	type: "dielectric";
	ior: number;
	roughness: number;
}

export type Material = LambertianMaterial | MetalMaterial | DielectricMaterial;

// --- Media ---

export interface NanoVDBMedium {
	type: "nanovdb";
	sigma_a: Vec3;
	sigma_s: Vec3;
	g: number;
	density_multiplier: number;
	scale?: number;
	translate?: Vec3;
	file: string;
}

export type Medium = NanoVDBMedium;

// --- Transforms ---

export interface Keyframe {
	time: number;
	translate?: Vec3;
	rotate?: Vec3;
	scale?: number | Vec3;
	curve?: InterpCurve;
}

export interface StaticTransform {
	translate?: Vec3;
	rotate?: Vec3;
	scale?: number | Vec3;
}

export interface AnimatedTransform {
	keyframes: Keyframe[];
}

export type NodeTransform = StaticTransform | AnimatedTransform;

export function isAnimated(
	t: NodeTransform | undefined,
): t is AnimatedTransform {
	return t !== undefined && Array.isArray((t as AnimatedTransform).keyframes);
}

// --- Graph nodes (JSON uses `type` on leaves; we use `kind` in TS) ---

interface NodeBase {
	name?: string;
	transform?: NodeTransform;
}

interface GroupNode extends NodeBase {
	kind: "group";
	children: SceneNode[];
}

export interface SphereNode extends NodeBase {
	kind: "sphere";
	material: string;
	center: Vec3;
	radius: number;
	visible?: boolean;
	inside_medium?: string;
	outside_medium?: string;
}

export interface QuadNode extends NodeBase {
	kind: "quad";
	material: string;
	vertices: [Vec3, Vec3, Vec3, Vec3];
	visible?: boolean;
}

export interface ObjNode extends NodeBase {
	kind: "obj";
	file: string;
	material?: string;
	auto_fit?: boolean;
	visible?: boolean;
}

export type SceneNode = GroupNode | SphereNode | QuadNode | ObjNode;

// --- Render settings ---

export interface ImageConfig {
	width: number;
	height: number;
	outfile?: string;
	exrfile?: string;
}

export interface RenderConfig {
	integrator: "path_trace" | "normals";
	max_samples: number;
	min_samples?: number;
	max_depth: number;
	threads: number;
	tile_size?: number;
	noise_threshold?: number;
	adaptive_step?: number;
	enable_deep?: boolean;
	transparent_background?: boolean;
	visibility_depth?: number;
	save_sample_map?: boolean;
	image: ImageConfig;
}

// --- Layer / Context (identical structure) ---

export interface LayerData {
	materials: Record<string, Material>;
	media?: Record<string, Medium>;
	graph: SceneNode[];
	render?: RenderConfig;
	visible?: boolean;
	/** Mirrors the layer-level "animated" flag the orchestrator peeks. */
	animated?: boolean;
}

// --- Animation (top-level scene.json block) ---

export interface Animation {
	start: number;
	end: number;
	fps: number;
	shutter_angle?: number;
}

export const DEFAULT_ANIMATION: Animation = {
	start: 0,
	end: 0,
	fps: 24,
	shutter_angle: 180,
};

export const DEFAULT_RENDER_CONFIG: RenderConfig = {
	integrator: "path_trace",
	max_samples: 128,
	min_samples: 16,
	max_depth: 8,
	threads: 0,
	noise_threshold: 0.01,
	enable_deep: false,
	image: { width: 1920, height: 1080 },
};

// --- scene.json top-level ---

export interface SceneManifest {
	camera: Camera;
	context: string[];
	layers: string[];
	output_dir: string;
	animation?: Animation;
}

// --- Resolved scene (what the app stores after loading) ---

export interface ResolvedLayer {
	name: string;
	path: string;
	data: LayerData;
}

export interface ResolvedScene {
	camera: Camera;
	contexts: ResolvedLayer[];
	layers: ResolvedLayer[];
	output_dir: string;
	animation: Animation;
	settings: RenderConfig;
}
