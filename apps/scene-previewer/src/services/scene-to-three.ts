// Converts a ResolvedScene into ThreeJS objects.
// OBJ files (and their MTL/texture dependencies) are read via the File System Access API.

import * as THREE from "three";
import { MTLLoader } from "three/examples/jsm/loaders/MTLLoader.js";
import { OBJLoader } from "three/examples/jsm/loaders/OBJLoader.js";
import type { Material, ResolvedScene, SceneNode } from "../types/scene";
import { getFile, readTextFile } from "./fs";
import {
	applyStaticTransformToObject3D,
	evaluateTransformAt,
} from "./transform";

export { applyStaticTransformToObject3D };

/** @deprecated Use applyStaticTransformToObject3D */
export const applyTransform = applyStaticTransformToObject3D;

function disposeMaterialTextures(material: THREE.Material) {
	for (const value of Object.values(material)) {
		if (value instanceof THREE.Texture) {
			value.dispose();
		} else if (Array.isArray(value)) {
			for (const item of value) {
				if (item instanceof THREE.Texture) item.dispose();
			}
		}
	}
}

function parseMtllibNames(objText: string): string[] {
	const names: string[] = [];
	for (const line of objText.split("\n")) {
		const match = line.trim().match(/^mtllib\s+(.+)$/);
		if (match) names.push(match[1].trim());
	}
	return names;
}

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
				...(hasEmission && { emissive }),
			});
		case "metal":
			return new THREE.MeshStandardMaterial({
				color,
				metalness: 1,
				roughness: mat.roughness,
				...(hasEmission && { emissive }),
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

async function buildLeafMesh(
	node: SceneNode,
	materials: Record<string, THREE.Material>,
	dir: FileSystemDirectoryHandle,
	blobUrls: string[],
): Promise<THREE.Object3D | null> {
	const fallback = new THREE.MeshLambertMaterial({ color: 0xcccccc });

	if (node.kind === "sphere") {
		const mesh = new THREE.Mesh(
			new THREE.SphereGeometry(node.radius, 32, 16),
			materials[node.material] ?? fallback,
		);
		mesh.position.set(...node.center);
		return mesh;
	}

	if (node.kind === "quad") {
		const [p0, p1, p2, p3] = node.vertices;
		const geo = new THREE.BufferGeometry();
		geo.setAttribute(
			"position",
			new THREE.BufferAttribute(
				new Float32Array([...p0, ...p1, ...p2, ...p0, ...p2, ...p3]),
				3,
			),
		);
		geo.computeVertexNormals();
		const baseMat = materials[node.material] ?? fallback;
		const quadMat = baseMat.clone();
		quadMat.side = THREE.DoubleSide;
		return new THREE.Mesh(geo, quadMat);
	}

	if (node.kind === "obj") {
		let objText: string;
		try {
			objText = await readTextFile(dir, node.file);
		} catch {
			console.warn(`[scene-to-three] OBJ load failed: ${node.file}`);
			return null;
		}

		const objBaseDir = node.file.includes("/")
			? `${node.file.split("/").slice(0, -1).join("/")}/`
			: "";

		let materialCreator: MTLLoader.MaterialCreator | null = null;
		for (const mtlName of parseMtllibNames(objText)) {
			let mtlText: string;
			try {
				mtlText = await readTextFile(dir, objBaseDir + mtlName);
			} catch {
				continue;
			}

			const urlMap = new Map<string, string>();
			for (const texName of parseMtlTexturePaths(mtlText)) {
				if (urlMap.has(texName)) continue;
				try {
					const file = await getFile(dir, objBaseDir + texName);
					const url = URL.createObjectURL(file);
					urlMap.set(texName, url);
					blobUrls.push(url);
				} catch {
					// texture missing
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
			break;
		}

		const objLoader = new OBJLoader();
		if (materialCreator) objLoader.setMaterials(materialCreator);
		const group = objLoader.parse(objText);

		const mat = node.material ? (materials[node.material] ?? null) : null;
		if (mat) {
			group.traverse((child) => {
				if (child instanceof THREE.Mesh) child.material = mat;
			});
		}

		if (node.auto_fit !== false) {
			const box = new THREE.Box3().setFromObject(group);
			const center = box.getCenter(new THREE.Vector3());
			const size = box.getSize(new THREE.Vector3());
			const s = 2 / Math.max(size.x, size.y, size.z, 0.001);
			group.scale.setScalar(s);
			group.position.copy(center).multiplyScalar(-s);
		}

		return group;
	}

	return null;
}

async function buildNode(
	node: SceneNode,
	indexPath: number[],
	tag: "ctx" | "lyr",
	layerIdx: number,
	materials: Record<string, THREE.Material>,
	dir: FileSystemDirectoryHandle,
	blobUrls: string[],
	signal: AbortSignal,
): Promise<THREE.Group | null> {
	if (signal.aborted) return null;

	const objectKey = `${tag}:${layerIdx}:${indexPath.join("/")}`;
	const nodeGroup = new THREE.Group();
	applyStaticTransformToObject3D(
		nodeGroup,
		evaluateTransformAt(node.transform, 0),
	);
	nodeGroup.userData.objectKey = objectKey;
	nodeGroup.userData.sceneNodeKind = node.kind;

	function tagSubtree(root: THREE.Object3D) {
		root.traverse((child) => {
			child.userData.objectKey = objectKey;
		});
	}

	if (node.kind === "group") {
		tagSubtree(nodeGroup);
		for (let ci = 0; ci < node.children.length; ci++) {
			if (signal.aborted) break;
			const child = await buildNode(
				node.children[ci],
				[...indexPath, ci],
				tag,
				layerIdx,
				materials,
				dir,
				blobUrls,
				signal,
			);
			if (child) nodeGroup.add(child);
		}
		return nodeGroup;
	}

	const mesh = await buildLeafMesh(node, materials, dir, blobUrls);
	if (!mesh) {
		tagSubtree(nodeGroup);
		return nodeGroup;
	}
	nodeGroup.add(mesh);
	tagSubtree(nodeGroup);
	return nodeGroup;
}

export interface SceneGraphResult {
	group: THREE.Group;
	blobUrls: string[];
}

export function revokeBlobUrls(urls: string[]) {
	for (const url of urls) {
		URL.revokeObjectURL(url);
	}
}

export async function buildSceneGraph(
	scene: ResolvedScene,
	dir: FileSystemDirectoryHandle,
	signal: AbortSignal,
): Promise<SceneGraphResult> {
	const root = new THREE.Group();
	const blobUrls: string[] = [];
	const disposableGroups: THREE.Group[] = [];

	const allLayers = [...scene.contexts, ...scene.layers];
	for (let li = 0; li < allLayers.length; li++) {
		if (signal.aborted) break;
		const layer = allLayers[li];
		const tag = li < scene.contexts.length ? "ctx" : "lyr";
		const idx = li < scene.contexts.length ? li : li - scene.contexts.length;

		const layerGroup = new THREE.Group();
		layerGroup.name = layer.name;
		disposableGroups.push(layerGroup);

		const threeMats: Record<string, THREE.Material> = {};
		for (const [name, mat] of Object.entries(layer.data.materials)) {
			threeMats[name] = makeThreeMaterial(mat);
		}

		for (let gi = 0; gi < layer.data.graph.length; gi++) {
			if (signal.aborted) break;
			const built = await buildNode(
				layer.data.graph[gi],
				[gi],
				tag,
				idx,
				threeMats,
				dir,
				blobUrls,
				signal,
			);
			if (built) layerGroup.add(built);
		}
		root.add(layerGroup);
	}

	if (signal.aborted) {
		revokeBlobUrls(blobUrls);
		for (const group of disposableGroups) {
			group.traverse((obj) => {
				if (obj instanceof THREE.Mesh) {
					obj.geometry?.dispose();
					const material = obj.material;
					if (Array.isArray(material)) {
						for (const mat of material) {
							disposeMaterialTextures(mat);
							mat.dispose();
						}
					} else if (material) {
						disposeMaterialTextures(material);
						material.dispose();
					}
				}
			});
		}
		return { group: root, blobUrls: [] };
	}

	return { group: root, blobUrls };
}
