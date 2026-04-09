// TypeScript types mirroring the Skewer scene JSON schema.
// Reference: skewer/src/io/scene_loader.cc

export type Vec3 = [number, number, number];

// --- Camera ---

export interface Camera {
	look_from: Vec3;
	look_at: Vec3;
	vup: Vec3;
	vfov: number;
	aperture_radius: number;
	focus_distance: number;
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

// --- Objects ---

export interface Transform {
	translate?: Vec3;
	rotate?: Vec3; // degrees, applied YXZ order
	scale?: number | Vec3;
}

export interface SphereObject {
	type: "sphere";
	material: string;
	center: Vec3;
	radius: number;
	visible?: boolean;
}

export interface QuadObject {
	type: "quad";
	material: string;
	vertices: [Vec3, Vec3, Vec3, Vec3];
	visible?: boolean;
}

export interface ObjFileObject {
	type: "obj";
	file: string;
	material?: string;
	auto_fit?: boolean;
	visible?: boolean;
	transform?: Transform;
}

export type SceneObject = SphereObject | QuadObject | ObjFileObject;

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
	objects: SceneObject[];
	render?: RenderConfig;
	visible?: boolean;
}

// --- scene.json top-level ---

export interface SceneManifest {
	camera: Camera;
	context: string[]; // relative paths to context JSON files
	layers: string[]; // relative paths to layer JSON files
	output_dir: string;
}

// --- Resolved scene (what the app stores after loading) ---

export interface ResolvedLayer {
	name: string; // filename stem, e.g. "layer_room"
	path: string; // relative path within the scene folder
	data: LayerData;
}

export interface ResolvedScene {
	camera: Camera;
	contexts: ResolvedLayer[];
	layers: ResolvedLayer[];
	output_dir: string;
}
