// Converts a ResolvedScene into ThreeJS objects.
// OBJ files are read from disk via the File System Access API.

import * as THREE from "three";
import { OBJLoader } from "three/examples/jsm/loaders/OBJLoader.js";
import type {
	Material,
	ResolvedScene,
	SceneObject,
	Transform,
} from "../types/scene";
import { readTextFile } from "./fs";

function makeThreeMaterial(mat: Material): THREE.Material {
	const color = new THREE.Color(mat.albedo[0], mat.albedo[1], mat.albedo[2]);
	const emissive = new THREE.Color(
		mat.emission[0],
		mat.emission[1],
		mat.emission[2],
	);
	const hasEmission = mat.emission.some((v) => v > 0);

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

function applyTransform(obj: THREE.Object3D, transform: Transform) {
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
			return new THREE.Mesh(geo, materials[obj.material] ?? fallback);
		}

		case "obj": {
			let text: string;
			try {
				text = await readTextFile(dir, obj.file);
			} catch {
				console.warn(`[scene-to-three] OBJ load failed: ${obj.file}`);
				return null;
			}

			const loader = new OBJLoader();
			const group = loader.parse(text);

			// Override all sub-mesh materials if specified
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

	for (const layer of [...scene.contexts, ...scene.layers]) {
		if (signal.aborted) break;

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

		for (const obj of built) {
			if (obj) layerGroup.add(obj);
		}
		root.add(layerGroup);
	}

	return root;
}
