import { describe, expect, it } from "vitest";
import {
	applyRedoEntry,
	applyUndoEntry,
	buildHistoryEntry,
} from "../../src/services/scene-history";
import type { ResolvedScene } from "../../src/types/scene";

describe("scene-history", () => {
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
});

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
