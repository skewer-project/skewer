import * as THREE from "three";
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { ResolvedScene } from "../../src/types/scene";
import { asDirectoryHandle, MemoryDirectoryHandle } from "./test-utils";

function makeScene(): ResolvedScene {
	return {
		camera: {
			look_from: [0, 0, 5],
			look_at: [0, 0, 0],
			vup: [0, 1, 0],
			vfov: 45,
			aperture_radius: 0,
			focus_distance: 5,
		},
		contexts: [],
		layers: [
			{
				name: "main",
				path: "main.json",
				data: {
					materials: {
						base: {
							type: "lambertian",
							albedo: [0.8, 0.8, 0.8],
							emission: [0, 0, 0],
							opacity: [1, 1, 1],
							visible: true,
						},
					},
					graph: [],
				},
			},
		],
		output_dir: "renders",
		animation: { start: 0, end: 0, fps: 24, shutter_angle: 180 },
		settings: {
			integrator: "path_trace",
			max_samples: 64,
			min_samples: 4,
			max_depth: 6,
			threads: 0,
			image: { width: 640, height: 360 },
		},
	};
}

function expectObjectAt(
	list: THREE.Object3D[],
	index: number,
	label: string,
): THREE.Object3D {
	const value = list[index];
	if (!value) throw new Error(`expected ${label} at index ${index}`);
	return value;
}

describe("scene-to-three", () => {
	beforeEach(() => {
		vi.restoreAllMocks();
	});

	it("assigns stable object keys through group and mesh subtrees", async () => {
		const { buildSceneGraph } = await import(
			"../../src/services/scene-to-three"
		);
		const scene = makeScene();
		const layer = scene.layers[0];
		if (!layer) throw new Error("expected layer");
		layer.data.graph = [
			{
				kind: "group",
				name: "root",
				children: [
					{
						kind: "sphere",
						name: "sphere",
						material: "base",
						center: [1, 2, 3],
						radius: 1,
					},
					{
						kind: "obj",
						name: "missing-obj",
						file: "missing.obj",
					},
				],
			},
		];

		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const result = await buildSceneGraph(
			scene,
			asDirectoryHandle(MemoryDirectoryHandle.fromFiles({})),
			new AbortController().signal,
		);

		const layerGroup = expectObjectAt(result.group.children, 0, "layer group");
		const rootGroup = expectObjectAt(layerGroup.children, 0, "root group");
		const sphereGroup = expectObjectAt(rootGroup.children, 0, "sphere group");
		const sphereMesh = expectObjectAt(sphereGroup.children, 0, "sphere mesh");
		const missingObjGroup = expectObjectAt(
			rootGroup.children,
			1,
			"missing obj group",
		);

		expect(rootGroup.userData.objectKey).toBe("lyr:0:0");
		expect(sphereGroup.userData.objectKey).toBe("lyr:0:0/0");
		expect(sphereMesh.userData.objectKey).toBe("lyr:0:0/0");
		expect(missingObjGroup.userData.objectKey).toBe("lyr:0:0/1");
		expect(missingObjGroup.children).toHaveLength(0);
		expect(warn).toHaveBeenCalledWith(
			"[scene-to-three] OBJ load failed: missing.obj",
		);
	});

	it("cleans up blob URLs and disposes built resources when construction is aborted", async () => {
		vi.resetModules();
		const controller = new AbortController();
		const createObjectURL = vi
			.spyOn(URL, "createObjectURL")
			.mockImplementation(() => "blob:texture-1");
		const revokeObjectURL = vi
			.spyOn(URL, "revokeObjectURL")
			.mockImplementation(() => {});
		const geometryDispose = vi.spyOn(THREE.BufferGeometry.prototype, "dispose");
		const materialDispose = vi.spyOn(THREE.Material.prototype, "dispose");

		vi.doMock("three/examples/jsm/loaders/MTLLoader.js", () => ({
			MTLLoader: class {
				parse() {
					return { preload() {} };
				}
			},
		}));
		vi.doMock("three/examples/jsm/loaders/OBJLoader.js", () => ({
			OBJLoader: class {
				setMaterials() {}
				parse() {
					const group = new THREE.Group();
					group.add(
						new THREE.Mesh(
							new THREE.BoxGeometry(1, 1, 1),
							new THREE.MeshBasicMaterial(),
						),
					);
					return group;
				}
			},
		}));
		vi.doMock("../../src/services/fs", () => ({
			readTextFile: vi.fn(
				async (_dir: FileSystemDirectoryHandle, path: string) => {
					if (path.endsWith(".obj")) {
						return "mtllib asset.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3";
					}
					return "newmtl base\nmap_Kd tex.png";
				},
			),
			getFile: vi.fn(async () => {
				controller.abort();
				return new File(["tex"], "tex.png");
			}),
		}));

		const { buildSceneGraph } = await import(
			"../../src/services/scene-to-three"
		);
		const scene = makeScene();
		const layer = scene.layers[0];
		if (!layer) throw new Error("expected layer");
		layer.data.graph = [
			{ kind: "obj", name: "mesh", file: "asset.obj" },
			{
				kind: "sphere",
				name: "after-abort",
				material: "base",
				center: [0, 0, 0],
				radius: 1,
			},
		];

		const result = await buildSceneGraph(
			scene,
			asDirectoryHandle(MemoryDirectoryHandle.fromFiles({})),
			controller.signal,
		);

		expect(result.blobUrls).toEqual([]);
		expect(revokeObjectURL).toHaveBeenCalledWith("blob:texture-1");
		expect(createObjectURL).toHaveBeenCalled();
		expect(geometryDispose).toHaveBeenCalled();
		expect(materialDispose).toHaveBeenCalled();
	});

	it("returns null for incomplete skyboxes instead of leaking partial texture URLs", async () => {
		vi.resetModules();
		vi.doUnmock("../../src/services/fs");
		vi.doUnmock("three/examples/jsm/loaders/MTLLoader.js");
		vi.doUnmock("three/examples/jsm/loaders/OBJLoader.js");

		const createObjectURL = vi
			.spyOn(URL, "createObjectURL")
			.mockImplementation((file: Blob | MediaSource) => {
				if (file instanceof File) return `blob:${file.name}`;
				return "blob:unknown";
			});
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});

		vi.doMock("three", async (importOriginal) => {
			const actual = await importOriginal<typeof import("three")>();
			class CubeTextureLoader {
				load(urls: string[]) {
					return { urls } as unknown as THREE.CubeTexture;
				}
			}
			return { ...actual, CubeTextureLoader };
		});

		const { buildSkyboxTexture } = await import(
			"../../src/services/scene-to-three"
		);
		const texture = await buildSkyboxTexture(
			{
				min: [-1, -1, -1],
				max: [1, 1, 1],
				faces: {
					"+x": "px.png",
					"-x": "missing.png",
				},
			},
			asDirectoryHandle(
				MemoryDirectoryHandle.fromFiles({
					"px.png": new Blob(["x"]),
				}),
			),
			[],
		);

		expect(texture).toBeNull();
		expect(createObjectURL).not.toHaveBeenCalled();
		expect(warn).toHaveBeenCalledWith(
			"[scene-to-three] Skybox face not found: missing.png",
		);
	});
});
