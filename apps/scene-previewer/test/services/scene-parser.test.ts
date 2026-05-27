import { beforeEach, describe, expect, it, vi } from "vitest";
import type { NanoVDBBounds } from "../../src/services/nanovdb-parser";
import { getNanoVDBBounds } from "../../src/services/nanovdb-parser";
import {
	loadScene,
	parseLayerData,
	parseSceneManifest,
} from "../../src/services/scene-parser";
import { asDirectoryHandle, MemoryDirectoryHandle } from "./test-utils";

vi.mock("../../src/services/nanovdb-parser", () => ({
	getNanoVDBBounds: vi.fn(),
}));

const camera = {
	look_from: [0, 1, 4],
	look_at: [0, 0, 0],
};

describe("scene-parser", () => {
	beforeEach(() => {
		vi.mocked(getNanoVDBBounds).mockReset();
	});

	it("parses the scene manifest contract", () => {
		const manifest = parseSceneManifest({
			camera: {
				...camera,
				keyframes: [
					{ time: 0, look_from: [0, 1, 4] },
					{ time: 2, look_at: [1, 0, 0], curve: { bezier: [0.2, 0, 0.8, 1] } },
				],
			},
			context: ["ctx/world.json"],
			layers: ["layers/shot.json"],
			output_dir: "out",
			animation: { start: 0, end: 2, fps: 24, shutter_angle: 270 },
			skybox: {
				min: [2, 0, 4],
				max: [-2, 0, 1],
				faces: { "+x": "sky/px.exr", "-x": "", ignored: "unused.exr" },
			},
		});

		expect(manifest.camera).toMatchObject({
			vup: [0, 1, 0],
			vfov: 90,
			aperture_radius: 0,
			focus_distance: 1,
			keyframes: [
				{ time: 0, look_from: [0, 1, 4] },
				{ time: 2, look_at: [1, 0, 0], curve: { bezier: [0.2, 0, 0.8, 1] } },
			],
		});
		expect(manifest.animation).toEqual({
			start: 0,
			end: 2,
			fps: 24,
			shutter_angle: 270,
		});
		expect(manifest.skybox).toEqual({
			min: [-2, 0, 1],
			max: [2, 0.1, 4],
			faces: { "+x": "sky/px.exr" },
		});
	});

	it("rejects legacy layers and reports nested errors", () => {
		expect(() => parseLayerData({ objects: [], graph: [] })).toThrow(
			"Layer must use 'graph' format",
		);

		expect(() =>
			parseLayerData({
				materials: {},
				graph: [
					{
						children: [
							{
								type: "sphere",
								material: "mat",
								center: [0, 0, 0],
								radius: 1,
								transform: { keyframes: [] },
							},
						],
					},
				],
			}),
		).toThrow(
			"graph[0].children[0].transform.keyframes: expected at least one keyframe",
		);
	});

	it("loads scene.json with referenced layers and synchronizes NanoVDB bounding spheres", async () => {
		const bounds: NanoVDBBounds = {
			centroid: [0, 0, 0],
			radius: 3,
			min: [0, 0, 0],
			max: [1, 1, 1],
		};
		vi.mocked(getNanoVDBBounds).mockResolvedValue(bounds);
		const dir = MemoryDirectoryHandle.fromFiles({
			"scene.json": JSON.stringify({
				camera,
				context: ["ctx.json"],
				layers: ["layer.json"],
				output_dir: "renders",
			}),
			"ctx.json": JSON.stringify({ materials: {}, graph: [] }),
			"layer.json": JSON.stringify({
				materials: { mat: { type: "lambertian" } },
				media: {
					smoke: {
						type: "nanovdb",
						file: "volumes/smoke.nvdb",
						scale: 2,
						translate: [4, 5, 6],
						rotate: [0, 45, 0],
					},
				},
				graph: [
					{
						type: "sphere",
						material: "null",
						inside_medium: "smoke",
					},
					{
						type: "sphere",
						material: "null",
						inside_medium: "smoke",
					},
				],
			}),
		});

		const scene = await loadScene(asDirectoryHandle(dir));

		expect(scene.output_dir).toBe("renders");
		expect(scene.animation).toEqual({
			start: 0,
			end: 0,
			fps: 24,
			shutter_angle: 180,
		});
		const spheres = scene.layers[0]?.data.graph;
		expect(spheres).toMatchObject([
			{
				kind: "sphere",
				center: [0, 0, 0],
				radius: 3,
				transform: { translate: [4, 5, 6], rotate: [0, 45, 0], scale: 2 },
			},
			{
				kind: "sphere",
				center: [0, 0, 0],
				radius: 3,
				transform: { translate: [4, 5, 6], rotate: [0, 45, 0], scale: 2 },
			},
		]);
		expect(getNanoVDBBounds).toHaveBeenCalledTimes(1);
		expect(getNanoVDBBounds).toHaveBeenCalledWith(
			asDirectoryHandle(dir),
			"volumes/smoke.nvdb",
		);
	});
});
