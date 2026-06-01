import { describe, expect, it, vi } from "vitest";
import {
	applyRedoEntry,
	applyUndoEntry,
	buildHistoryEntry,
	canCoalesceHistoryEntries,
	coalesceHistoryEntries,
	serializeSceneFiles,
} from "../../src/services/scene-history";
import type { ResolvedScene } from "../../src/types/scene";

function makeScene(): ResolvedScene {
	return {
		camera: {
			look_from: [0, 1, 5],
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
							albedo: [1, 0, 0],
							emission: [0, 0, 0],
							opacity: [1, 1, 1],
							visible: true,
						},
					},
					graph: [
						{
							kind: "group",
							name: "root",
							children: [
								{
									kind: "sphere",
									name: "left",
									material: "base",
									center: [0, 0, 0],
									radius: 1,
								},
								{
									kind: "sphere",
									name: "right",
									material: "base",
									center: [2, 0, 0],
									radius: 2,
								},
							],
						},
					],
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

function makeSceneWithSkybox(): ResolvedScene {
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
		animation: { start: 0, end: 1, fps: 24, shutter_angle: 180 },
		settings: {
			integrator: "path_trace",
			max_samples: 128,
			min_samples: 16,
			max_depth: 8,
			threads: 0,
			image: { width: 1920, height: 1080 },
		},
		skybox: {
			min: [-10, -10, -10],
			max: [10, 10, 10],
			faces: {
				"+x": "skybox/px.exr",
				"-x": "skybox/nx.exr",
			},
		},
	};
}

function makeSceneWithVolume(): ResolvedScene {
	const scene = makeSceneWithSkybox();
	scene.skybox = undefined;
	scene.layers[0].data.media = {
		smoke: {
			type: "nanovdb",
			file: "volumes/smoke.nvdb",
			sigma_a: [0, 0, 0],
			sigma_s: [1, 1, 1],
			g: 0,
			density_multiplier: 1,
			scale: 3,
			translate: [10, 20, 30],
			rotate: [0, 90, 0],
		},
	};
	scene.layers[0].data.graph = [
		{
			kind: "sphere",
			material: "null",
			center: [0, 0, 0],
			radius: 4,
			inside_medium: "smoke",
			transform: {
				translate: [10, 20, 30],
				rotate: [0, 90, 0],
				scale: 3,
			},
		},
	];
	return scene;
}

describe("scene-history", () => {
	it("diffs array insertion as a single add and can undo/redo it cleanly", () => {
		const before = makeScene();
		const inserted = structuredClone(before);
		inserted.layers[0].data.graph[0].children.splice(1, 0, {
			kind: "sphere",
			name: "middle",
			material: "base",
			center: [1, 0, 0],
			radius: 0.5,
		});

		const entry = buildHistoryEntry(before, inserted, "insert child");
		expect(entry).not.toBeNull();
		if (!entry) {
			throw new Error("expected history entry");
		}
		expect(entry.deltas).toEqual([
			expect.objectContaining({
				operation: "add",
				filePath: "main.json",
				jsonPath: "/graph/0/children/1",
				newValue: expect.objectContaining({ name: "middle" }),
			}),
		]);

		expect(serializeSceneFiles(applyUndoEntry(inserted, entry))).toEqual(
			serializeSceneFiles(before),
		);
		expect(serializeSceneFiles(applyRedoEntry(before, entry))).toEqual(
			serializeSceneFiles(inserted),
		);
	});

	it("treats vec3 edits atomically and preserves old values across coalesced drags", () => {
		vi.useFakeTimers();
		vi.setSystemTime(new Date("2026-05-28T12:00:00Z"));

		const start = makeScene();
		const mid = structuredClone(start);
		const midLeft = mid.layers[0].data.graph[0].children[0];
		if (midLeft?.kind !== "sphere") {
			throw new Error("expected left sphere");
		}
		midLeft.center = [1, 2, 3];
		const end = structuredClone(mid);
		const endLeft = end.layers[0].data.graph[0].children[0];
		if (endLeft?.kind !== "sphere") {
			throw new Error("expected left sphere");
		}
		endLeft.center = [4, 5, 6];

		const first = buildHistoryEntry(start, mid, "drag");
		vi.advanceTimersByTime(200);
		const second = buildHistoryEntry(mid, end, "drag");
		if (!first || !second) {
			throw new Error("expected drag history entries");
		}

		expect(first.deltas).toEqual([
			expect.objectContaining({
				operation: "update",
				jsonPath: "/graph/0/children/0/center",
				oldValue: [0, 0, 0],
				newValue: [1, 2, 3],
			}),
		]);
		expect(canCoalesceHistoryEntries(first, second)).toBe(true);

		const merged = coalesceHistoryEntries(first, second);
		expect(merged.deltas).toEqual([
			expect.objectContaining({
				jsonPath: "/graph/0/children/0/center",
				oldValue: [0, 0, 0],
				newValue: [4, 5, 6],
			}),
		]);
		expect(serializeSceneFiles(applyUndoEntry(end, merged))).toEqual(
			serializeSceneFiles(start),
		);
		expect(serializeSceneFiles(applyRedoEntry(start, merged))).toEqual(
			serializeSceneFiles(end),
		);
	});

	it("preserves skybox data when undoing and redoing unrelated scene edits", () => {
		const before = makeSceneWithSkybox();
		const after = makeSceneWithSkybox();
		after.layers[0].data.graph = [
			{
				kind: "sphere",
				material: "mat",
				center: [0, 0, 0],
				radius: 1,
			},
		];

		const entry = buildHistoryEntry(before, after, "Add sphere");

		expect(entry).not.toBeNull();
		if (!entry) throw new Error("Expected history entry");

		const undone = applyUndoEntry(after, entry);
		expect(undone.skybox).toEqual(before.skybox);
		expect(undone.layers[0].data.graph).toEqual([]);

		const redone = applyRedoEntry(undone, entry);
		expect(redone.skybox).toEqual(before.skybox);
		expect(redone.layers[0].data.graph).toEqual(after.layers[0].data.graph);
	});

	it("preserves resolved volumetric sphere geometry during history hydration", () => {
		const before = makeSceneWithVolume();
		const after = makeSceneWithVolume();
		after.layers[0].data.materials.extra = {
			type: "lambertian",
			albedo: [0.5, 0.5, 0.5],
			emission: [0, 0, 0],
			opacity: [1, 1, 1],
			visible: true,
		};

		const entry = buildHistoryEntry(before, after, "Add material");

		expect(entry).not.toBeNull();
		if (!entry) throw new Error("Expected history entry");

		const undone = applyUndoEntry(after, entry);
		expect(undone.layers[0].data.graph[0]).toEqual(
			before.layers[0].data.graph[0],
		);
		expect(undone.layers[0].data.materials).not.toHaveProperty("extra");

		const redone = applyRedoEntry(undone, entry);
		expect(redone.layers[0].data.graph[0]).toEqual(
			before.layers[0].data.graph[0],
		);
		expect(redone.layers[0].data.materials).toHaveProperty("extra");
	});
});
