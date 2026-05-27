import { afterEach, describe, expect, it, vi } from "vitest";
import { collectSceneBundle } from "../../src/services/bundle";
import type { ResolvedScene } from "../../src/types/scene";
import {
	asDirectoryHandle,
	bundleText,
	MemoryDirectoryHandle,
} from "./test-utils";

describe("bundle", () => {
	afterEach(() => {
		vi.restoreAllMocks();
	});

	it("collects the complete cloud-render bundle while deduping assets", async () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const scene = makeScene();
		const dir = MemoryDirectoryHandle.fromFiles({
			"textures/albedo.png": "albedo",
			"sky/px.exr": "px",
			"volumes/smoke.nvdb": "nvdb",
			"models/hero.obj": "mtllib materials/hero.mtl\nv 0 0 0\n",
			"models/materials/hero.mtl": [
				"newmtl mat",
				"map_Kd -s 1 1 1 textures/diffuse.jpg",
				"norm textures/normal.png",
				"map_bump missing-bump.png",
			].join("\n"),
			"models/textures/diffuse.jpg": "diffuse",
			"models/textures/normal.png": "normal",
		});

		const bundle = await collectSceneBundle(scene, asDirectoryHandle(dir));

		expect(bundle.map((file) => file.path)).toEqual([
			"scene.json",
			"ctx.json",
			"layer.json",
			"textures/albedo.png",
			"sky/px.exr",
			"volumes/smoke.nvdb",
			"models/hero.obj",
			"models/materials/hero.mtl",
			"models/textures/diffuse.jpg",
			"models/textures/normal.png",
		]);
		expect(
			bundle.find((file) => file.path === "textures/albedo.png"),
		).toMatchObject({
			contentType: "image/png",
		});
		expect(
			bundle.find((file) => file.path === "models/hero.obj"),
		).toMatchObject({
			contentType: "model/obj",
		});
		expect(JSON.parse(await bundleText(bundle, "scene.json"))).toMatchObject({
			context: ["ctx.json"],
			layers: ["layer.json"],
			skybox: { faces: { "+x": "sky/px.exr" } },
		});
		expect(JSON.parse(await bundleText(bundle, "layer.json"))).toMatchObject({
			media: { smoke: { file: "volumes/smoke.nvdb" } },
			graph: [{ type: "obj", file: "models/hero.obj" }],
		});
		expect(warn).toHaveBeenCalledWith(
			"[collectSceneBundle] texture not found: models/missing-bump.png",
		);
	});

	it("fails fast for out-of-dir or missing required assets", async () => {
		const scene = makeScene();
		scene.layers[0].data.materials.mat.albedo_texture = "../outside.png";
		const dir = MemoryDirectoryHandle.fromFiles({});

		await expect(
			collectSceneBundle(scene, asDirectoryHandle(dir)),
		).rejects.toThrow('Path must not contain "..": ../outside.png');

		scene.contexts[0].data.materials.mat.albedo_texture = undefined;
		scene.layers[0].data.materials.mat.albedo_texture = "textures/missing.png";
		await expect(
			collectSceneBundle(scene, asDirectoryHandle(dir)),
		).rejects.toThrow(
			"Missing material texture for bundle: textures/missing.png",
		);
	});
});

function makeScene(): ResolvedScene {
	return {
		camera: {
			look_from: [0, 0, 5],
			look_at: [0, 0, 0],
			vup: [0, 1, 0],
			vfov: 45,
			aperture_radius: 0,
			focus_distance: 1,
		},
		contexts: [
			{
				name: "ctx",
				path: "ctx.json",
				data: {
					materials: {
						mat: {
							type: "lambertian",
							albedo: [1, 1, 1],
							emission: [0, 0, 0],
							opacity: [1, 1, 1],
							visible: true,
							albedo_texture: "textures/albedo.png",
						},
					},
					graph: [],
				},
			},
		],
		layers: [
			{
				name: "layer",
				path: "layer.json",
				data: {
					materials: {
						mat: {
							type: "lambertian",
							albedo: [1, 1, 1],
							emission: [0, 0, 0],
							opacity: [1, 1, 1],
							visible: true,
							albedo_texture: "textures/albedo.png",
						},
					},
					media: {
						smoke: {
							type: "nanovdb",
							file: "volumes/smoke.nvdb",
							sigma_a: [0, 0, 0],
							sigma_s: [1, 1, 1],
							g: 0,
							density_multiplier: 1,
						},
					},
					graph: [{ kind: "obj", file: "models/hero.obj", material: "mat" }],
				},
			},
		],
		output_dir: "",
		animation: { start: 0, end: 0, fps: 24, shutter_angle: 180 },
		settings: {
			integrator: "path_trace",
			max_samples: 1,
			max_depth: 1,
			threads: 0,
			image: { width: 1, height: 1 },
		},
		skybox: {
			min: [-1, -1, -1],
			max: [1, 1, 1],
			faces: { "+x": "sky/px.exr" },
		},
	};
}
