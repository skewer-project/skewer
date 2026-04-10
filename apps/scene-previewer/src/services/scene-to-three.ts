// Converts a ResolvedScene into ThreeJS objects.
// OBJ files (and their MTL/texture dependencies) are read via the File System Access API.

import * as THREE from "three";
import { MTLLoader } from "three/examples/jsm/loaders/MTLLoader.js";
import { OBJLoader } from "three/examples/jsm/loaders/OBJLoader.js";
import type {
	Material,
	ResolvedScene,
	SceneObject,
	Transform,
} from "../types/scene";
import { getFile, readTextFile } from "./fs";

// Extracts `mtllib <name>` lines from OBJ text.
function parseMtllibNames(objText: string): string[] {
	const names: string[] = [];
	for (const line of objText.split("\n")) {
		const match = line.trim().match(/^mtllib\s+(.+)$/);
		if (match) names.push(match[1].trim());
	}
	return names;
}

// Extracts `map_Kd <path>` lines from MTL text.
function parseMtlTexturePaths(mtlText: string): string[] {
	const paths: string[] = [];
	for (const line of mtlText.split("\n")) {
		const match = line.trim().match(/^map_Kd\s+(.+)$/i);
		if (match) paths.push(match[1].trim());
	}
	return paths;
}

export function makeThreeMaterial(mat: Material): THREE.Material {
	const color = new THREE.Color(mat.albedo[0], mat.albedo[1], mat.albedo[2]);
	const hasEmission = mat.emission.some((v) => v > 0);
	const emissive = (() => {
		if (!hasEmission) return new THREE.Color(0, 0, 0);
		const peak = Math.max(...mat.emission);
		return new THREE.Color(
			mat.emission[0] / peak,
			mat.emission[1] / peak,
			mat.emission[2] / peak,
		);
	})();

	switch (mat.type) {
		case "lambertian":
			return new THREE.MeshLambertMaterial({
				color,
				emissive: hasEmission ? emissive : undefined,
			});
		case "metal":
			return new THREE.MeshStandardMaterial({
				color,
				metalness: 1,
				roughness: mat.roughness,
				emissive: hasEmission ? emissive : undefined,
			});
		case "dielectric":
			return new THREE.MeshPhysicalMaterial({
				color,
				transmission: 1,
				ior: mat.ior,
				roughness: mat.roughness,
				transparent: true,
			});
	}
}

export function applyTransform(obj: THREE.Object3D, transform: Transform) {
	if (transform.translate) obj.position.set(...transform.translate);
	if (transform.rotate) {
		obj.rotation.order = "YXZ";
		obj.rotation.set(
			THREE.MathUtils.degToRad(transform.rotate[0]),
			THREE.MathUtils.degToRad(transform.rotate[1]),
			THREE.MathUtils.degToRad(transform.rotate[2]),
		);
	}
	if (transform.scale !== undefined) {
		if (typeof transform.scale === "number")
			obj.scale.setScalar(transform.scale);
		else obj.scale.set(...transform.scale);
	}
}

async function buildObject(
	obj: SceneObject,
	materials: Record<string, THREE.Material>,
	dir: FileSystemDirectoryHandle,
): Promise<THREE.Object3D | null> {
	const fallback = new THREE.MeshLambertMaterial({ color: 0xcccccc });

	switch (obj.type) {
		case "sphere": {
			const mesh = new THREE.Mesh(
				new THREE.SphereGeometry(obj.radius, 32, 16),
				materials[obj.material] ?? fallback,
			);
			mesh.position.set(...obj.center);
			return mesh;
		}

		case "quad": {
			const [p0, p1, p2, p3] = obj.vertices;
			const geo = new THREE.BufferGeometry();
			// Two triangles: (p0,p1,p2) and (p0,p2,p3) — matches Skewer's CreateQuad winding
			geo.setAttribute(
				"position",
				new THREE.BufferAttribute(
					new Float32Array([...p0, ...p1, ...p2, ...p0, ...p2, ...p3]),
					3,
				),
			);
			geo.computeVertexNormals();
			const baseMat = materials[obj.material] ?? fallback;
			const quadMat = baseMat.clone();
			quadMat.side = THREE.DoubleSide;
			return new THREE.Mesh(geo, quadMat);
		}

		case "obj": {
			let objText: string;
			try {
				objText = await readTextFile(dir, obj.file);
			} catch {
				console.warn(`[scene-to-three] OBJ load failed: ${obj.file}`);
				return null;
			}

			// Base directory of the OBJ file — MTL and textures live alongside it.
			const objBaseDir = obj.file.includes("/")
				? `${obj.file.split("/").slice(0, -1).join("/")}/`
				: "";

			// Load MTL materials and any referenced textures.
			let materialCreator: MTLLoader.MaterialCreator | null = null;
			for (const mtlName of parseMtllibNames(objText)) {
				let mtlText: string;
				try {
					mtlText = await readTextFile(dir, objBaseDir + mtlName);
				} catch {
					continue;
				}

				// Pre-load textures as blob URLs so MTLLoader can resolve them
				// without needing HTTP. Maps bare filename → blob URL.
				const urlMap = new Map<string, string>();
				for (const texName of parseMtlTexturePaths(mtlText)) {
					if (urlMap.has(texName)) continue;
					try {
						const file = await getFile(dir, objBaseDir + texName);
						urlMap.set(texName, URL.createObjectURL(file));
					} catch {
						// texture missing — MTLLoader will use a default color
					}
				}

				const manager = new THREE.LoadingManager();
				manager.setURLModifier((url) => {
					const bare = url.split("/").pop() ?? url;
					return urlMap.get(bare) ?? urlMap.get(url) ?? url;
				});

				const mtlLoader = new MTLLoader(manager);
				materialCreator = mtlLoader.parse(mtlText, "");
				materialCreator.preload();
				break; // use first MTL file only
			}

			const objLoader = new OBJLoader();
			if (materialCreator) objLoader.setMaterials(materialCreator);
			const group = objLoader.parse(objText);

			// Scene JSON material override takes precedence over MTL materials.
			const mat = obj.material ? (materials[obj.material] ?? null) : null;
			if (mat) {
				group.traverse((child) => {
					if (child instanceof THREE.Mesh) child.material = mat;
				});
			}

			// auto_fit (default true): center at origin, scale to 2-unit cube
			if (obj.auto_fit !== false) {
				const box = new THREE.Box3().setFromObject(group);
				const center = box.getCenter(new THREE.Vector3());
				const size = box.getSize(new THREE.Vector3());
				const s = 2 / Math.max(size.x, size.y, size.z, 0.001);
				group.scale.setScalar(s);
				group.position.copy(center).multiplyScalar(-s);
			}

			// Wrap so JSON transform applies on top of auto_fit
			const wrapper = new THREE.Group();
			wrapper.add(group);
			if (obj.transform) applyTransform(wrapper, obj.transform);
			return wrapper;
		}
	}
}

export async function buildSceneGraph(
	scene: ResolvedScene,
	dir: FileSystemDirectoryHandle,
	signal: AbortSignal,
): Promise<THREE.Group> {
	const root = new THREE.Group();

	const allLayers = [...scene.contexts, ...scene.layers];
	for (let li = 0; li < allLayers.length; li++) {
		if (signal.aborted) break;
		const layer = allLayers[li];
		const tag = li < scene.contexts.length ? "ctx" : "lyr";
		const idx = li < scene.contexts.length ? li : li - scene.contexts.length;

		const layerGroup = new THREE.Group();
		layerGroup.name = layer.name;

		// Resolve materials for this layer
		const threeMats: Record<string, THREE.Material> = {};
		for (const [name, mat] of Object.entries(layer.data.materials)) {
			threeMats[name] = makeThreeMaterial(mat);
		}

		// Build objects (in parallel per layer)
		const built = await Promise.all(
			layer.data.objects.map((obj) => buildObject(obj, threeMats, dir)),
		);
		if (signal.aborted) break;

		for (let oi = 0; oi < built.length; oi++) {
			const obj = built[oi];
			if (obj) {
				obj.userData.objectKey = `${tag}:${idx}:${oi}`;
				// Tag all descendant meshes too so raycaster can find the key
				obj.traverse((child) => {
					child.userData.objectKey = `${tag}:${idx}:${oi}`;
				});
				layerGroup.add(obj);
			}
		}
		root.add(layerGroup);
	}

	return root;
}
