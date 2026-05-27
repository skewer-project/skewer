import { describe, expect, it } from "vitest";
import {
	applyStaticTransformToAnimatedAtTime,
	collectAnimatedNodeTracks,
	collectSceneKeyframeTimes,
	evaluateCameraAt,
	evaluateTransformAt,
	getAnimationRange,
	layerHasKeyframes,
} from "../../src/services/transform";
import type {
	AnimatedTransform,
	Camera,
	ResolvedScene,
} from "../../src/types/scene";

describe("transform", () => {
	it("evaluates animated transforms in time order with inherited partial keyframes", () => {
		const transform: AnimatedTransform = {
			keyframes: [
				{ time: 2, translate: [10, 0, 0], scale: 3 },
				{ time: 0, translate: [0, 0, 0], rotate: [0, 0, 0], scale: 1 },
				{ time: 1, rotate: [0, 90, 0] },
			],
		};

		expect(evaluateTransformAt(transform, -1)).toEqual({
			translate: [0, 0, 0],
			rotate: [0, 0, 0],
			scale: 1,
		});
		const atOne = evaluateTransformAt(transform, 1);
		expect(atOne.translate).toEqual([0, 0, 0]);
		expect(atOne.rotate?.[0]).toBeCloseTo(0);
		expect(atOne.rotate?.[1]).toBeCloseTo(90);
		expect(atOne.rotate?.[2]).toBeCloseTo(0);
		expect(atOne.scale).toBe(1);
		expect(evaluateTransformAt(transform, 3)).toEqual({
			translate: [10, 0, 0],
			rotate: [0, 90, 0],
			scale: 3,
		});
		expect(evaluateTransformAt(transform, 1.5).translate?.[0]).toBeCloseTo(5);
	});

	it("updates animated transforms at a time while preserving existing keyframe metadata", () => {
		const updated = applyStaticTransformToAnimatedAtTime(
			{
				keyframes: [
					{ time: 2, translate: [2, 0, 0] },
					{ time: 1, translate: [1, 0, 0], curve: "ease-out" },
				],
			},
			1.00001,
			{ translate: [5, 5, 5], rotate: [0, 45, 0], scale: [1, 2, 1] },
		);

		expect(updated.keyframes).toEqual([
			{
				time: 1,
				translate: [5, 5, 5],
				rotate: [0, 45, 0],
				scale: [1, 2, 1],
				curve: "ease-out",
			},
			{ time: 2, translate: [2, 0, 0] },
		]);

		const inserted = applyStaticTransformToAnimatedAtTime(updated, 0, {
			translate: [-1, 0, 0],
		});
		expect(inserted.keyframes.map((kf) => kf.time)).toEqual([0, 1, 2]);
		expect(inserted.keyframes[0]?.curve).toBe("linear");
	});

	it("evaluates camera keyframes by inheriting omitted fields from earlier keyframes", () => {
		const camera: Camera = {
			look_from: [0, 0, 5],
			look_at: [0, 0, 0],
			vup: [0, 1, 0],
			vfov: 50,
			aperture_radius: 0,
			focus_distance: 1,
			keyframes: [
				{ time: 0, look_from: [0, 0, 5], vfov: 40 },
				{ time: 2, look_at: [2, 0, 0], focus_distance: 10 },
			],
		};

		expect(evaluateCameraAt(camera, 2)).toMatchObject({
			look_from: [0, 0, 5],
			look_at: [2, 0, 0],
			vfov: 40,
			focus_distance: 10,
			keyframes: camera.keyframes,
		});
		expect(evaluateCameraAt(camera, 1)).toMatchObject({
			look_from: [0, 0, 5],
			look_at: [1, 0, 0],
			vfov: 40,
			focus_distance: 5.5,
		});
	});

	it("collects scene animation timing across nested context and layer graphs", () => {
		const scene = makeAnimatedScene();

		expect(getAnimationRange(scene)).toEqual({ start: 0, end: 3 });
		expect(collectSceneKeyframeTimes(scene)).toEqual([0, 1, 3]);
		expect(layerHasKeyframes(scene.layers[0].data.graph)).toBe(true);
		expect(collectAnimatedNodeTracks(scene).map((track) => track.key)).toEqual([
			"ctx:0:0",
			"ctx:0:0/0",
			"lyr:0:0",
		]);
	});
});

function makeAnimatedScene(): ResolvedScene {
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
					materials: {},
					graph: [
						{
							kind: "group",
							name: "rig",
							children: [
								{
									kind: "sphere",
									material: "mat",
									center: [0, 0, 0],
									radius: 1,
									transform: {
										keyframes: [{ time: 0 }, { time: 3, translate: [3, 0, 0] }],
									},
								},
							],
						},
					],
				},
			},
		],
		layers: [
			{
				name: "layer",
				path: "layer.json",
				data: {
					materials: {},
					graph: [
						{
							kind: "obj",
							file: "hero.obj",
							transform: {
								keyframes: [{ time: 1, rotate: [0, 30, 0] }],
							},
						},
					],
				},
			},
		],
		output_dir: "",
		animation: { start: 0, end: 3, fps: 24 },
		settings: {
			integrator: "path_trace",
			max_samples: 1,
			max_depth: 1,
			threads: 0,
			image: { width: 1, height: 1 },
		},
	};
}
