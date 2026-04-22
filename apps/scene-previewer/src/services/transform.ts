import * as THREE from "three";
import type { NodeTransform, StaticTransform } from "../types/scene";
import { isAnimated } from "../types/scene";

/** Evaluate transform at time `time` (seconds). Animated: nearest keyframe / clamp at ends. */
export function evaluateTransformAt(
	t: NodeTransform | undefined,
	time: number,
): StaticTransform {
	if (t === undefined) return {};
	if (!isAnimated(t)) return { ...t };

	const kf = t.keyframes;
	if (kf.length === 0) return {};

	const sorted = [...kf].sort((a, b) => a.time - b.time);
	let chosen = sorted[0];
	for (const k of sorted) {
		if (k.time <= time) chosen = k;
		else break;
	}
	const best = chosen;

	const out: StaticTransform = {};
	if (best.translate !== undefined)
		out.translate = [...best.translate] as [number, number, number];
	if (best.rotate !== undefined)
		out.rotate = [...best.rotate] as [number, number, number];
	if (best.scale !== undefined) out.scale = best.scale;
	return out;
}

export function applyStaticTransformToObject3D(
	obj: THREE.Object3D,
	transform: StaticTransform,
) {
	if (transform.translate) obj.position.set(...transform.translate);
	else obj.position.set(0, 0, 0);

	if (transform.rotate) {
		obj.rotation.order = "YXZ";
		obj.rotation.set(
			THREE.MathUtils.degToRad(transform.rotate[0]),
			THREE.MathUtils.degToRad(transform.rotate[1]),
			THREE.MathUtils.degToRad(transform.rotate[2]),
		);
	} else {
		obj.rotation.set(0, 0, 0);
	}

	if (transform.scale !== undefined) {
		if (typeof transform.scale === "number")
			obj.scale.setScalar(transform.scale);
		else obj.scale.set(...transform.scale);
	} else {
		obj.scale.set(1, 1, 1);
	}
}
