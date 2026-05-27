import { beforeEach, describe, expect, it, vi } from "vitest";
import { getNanoVDBBounds } from "../../src/services/nanovdb-parser";
import {
	saveScene,
	serializeLayerData,
	serializeSceneJSON,
} from "../../src/services/scene-serializer";
import type { ResolvedScene } from "../../src/types/scene";
import { asDirectoryHandle, MemoryDirectoryHandle } from "./test-utils";

vi.mock("../../src/services/nanovdb-parser", () => ({
	getNanoVDBBounds: vi.fn(),
}));

describe("scene-serializer", () => {
	beforeEach(() => {
		vi.mocked(getNanoVDBBounds).mockReset();
	});

	it("serializes animation contracts", () => {
		const scene = makeScene();
		scene.camera.keyframes = [];
		scene.layers[0].data.graph = [
			{
				kind: "group",
				name: "animated-group",
				transform: {
					keyframes: [
						{ time: 1, translate: [1, 0, 0], curve: "ease-in" },
						{ time: 0, scale: [1, 2, 3] },
					],
				},
				children: [
					{
						kind: "obj",
						file: "models/hero.obj",
						material: "mat",
						transform: { translate: [0, 1, 0] },
					},
				],
			},
		];

		const manifest = JSON.parse(serializeSceneJSON(scene));
		const layer = serializeLayerData(scene.layers[0].data);

		expect(manifest.camera).not.toHaveProperty("keyframes");
		expect(manifest.animation).toEqual({
			start: 0,
			end: 2,
			fps: 24,
			shutter_angle: 180,
		});
		expect(layer).toMatchObject({
			animated: true,
			graph: [
				{
					name: "animated-group",
					transform: {
						keyframes: [
							{ time: 1, translate: [1, 0, 0], curve: "ease-in" },
							{ time: 0, scale: [1, 2, 3] },
						],
					},
					children: [
						{
							type: "obj",
							file: "models/hero.obj",
							material: "mat",
							transform: { translate: [0, 1, 0] },
						},
					],
				},
			],
		});
	});

	it("preserves explicit layer animated=false even when keyframes exist", () => {
		const data = makeScene().layers[0].data;
		data.animated = false;
		data.graph = [
			{
				kind: "sphere",
				material: "mat",
				center: [0, 0, 0],
				radius: 1,
				transform: { keyframes: [{ time: 0, translate: [1, 2, 3] }] },
			},
		];

		expect(serializeLayerData(data).animated).toBe(false);
	});

	it("saveScene synchronizes NanoVDB media and does not mutate app state", async () => {
		vi.mocked(getNanoVDBBounds).mockResolvedValue({
			centroid: [0, 0, 0],
			radius: 4,
			min: [0, 0, 0],
			max: [1, 1, 1],
		});
		const dir = MemoryDirectoryHandle.fromFiles({});
		const scene = makeScene();
		scene.layers[0].data.media = {
			smoke: {
				type: "nanovdb",
				file: "volumes/smoke.nvdb",
				sigma_a: [0, 0, 0],
				sigma_s: [1, 1, 1],
				g: 0,
				density_multiplier: 1,
			},
		};
		scene.layers[0].data.graph = [
			{
				kind: "sphere",
				material: "null",
				center: [1, 2, 3],
				radius: 8,
				inside_medium: "smoke",
				transform: { translate: [10, 20, 30], rotate: [0, 90, 0], scale: 2 },
			},
		];

		await saveScene(asDirectoryHandle(dir), scene);

		expect(scene.layers[0].data.media.smoke).not.toHaveProperty("translate");
		const writtenLayer = JSON.parse(await dir.text("layer.json"));
		expect(writtenLayer.media.smoke).toMatchObject({
			file: "volumes/smoke.nvdb",
			translate: [11, 22, 33],
			rotate: [0, 90, 0],
			scale: 4,
		});
		expect(JSON.parse(await dir.text("scene.json"))).toMatchObject({
			layers: ["layer.json"],
			context: [],
		});
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
		contexts: [],
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
						},
					},
					graph: [],
				},
			},
		],
		output_dir: "",
		animation: { start: 0, end: 2, fps: 24, shutter_angle: 180 },
		settings: {
			integrator: "path_trace",
			max_samples: 128,
			min_samples: 16,
			max_depth: 8,
			threads: 0,
			image: { width: 1920, height: 1080 },
		},
	};
}
