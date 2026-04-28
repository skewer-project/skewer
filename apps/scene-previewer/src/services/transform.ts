import * as THREE from "three";
import { formatObjectPathKey } from "./graph-path";
import { displayLabel } from "./node-labels";
import type {
	AnimatedTransform,
	InterpCurve,
	Keyframe,
	NodeTransform,
	ResolvedScene,
	SceneNode,
	StaticTransform,
	Vec3,
} from "../types/scene";
import { isAnimated } from "../types/scene";

const BEZIER_NEWTON_EPS = 1e-6;
const BEZIER_NEWTON_MAX_ITER = 32;

/** Resolved TRS after JSON accumulation (same order as C++ ParseAnimatedTransformJson). */
interface ResolvedKeyframe {
	time: number;
	translate: Vec3;
	rotate: Vec3;
	scale: Vec3;
	curve?: InterpCurve;
}

function lerpVec3(a: Vec3, b: Vec3, s: number): Vec3 {
	return [
		a[0] + (b[0] - a[0]) * s,
		a[1] + (b[1] - a[1]) * s,
		a[2] + (b[2] - a[2]) * s,
	];
}

function eulerDegToQuat(e: Vec3): THREE.Quaternion {
	const ord = new THREE.Euler(
		THREE.MathUtils.degToRad(e[0]),
		THREE.MathUtils.degToRad(e[1]),
		THREE.MathUtils.degToRad(e[2]),
		"YXZ",
	);
	return new THREE.Quaternion().setFromEuler(ord);
}

function quatToEulerDeg(q: THREE.Quaternion): Vec3 {
	const e = new THREE.Euler().setFromQuaternion(q, "YXZ");
	return [
		THREE.MathUtils.radToDeg(e.x),
		THREE.MathUtils.radToDeg(e.y),
		THREE.MathUtils.radToDeg(e.z),
	];
}

function curveToBezierControlPoints(curve: InterpCurve | undefined): {
	p1x: number;
	p1y: number;
	p2x: number;
	p2y: number;
} {
	if (curve === undefined || curve === "linear") {
		return { p1x: 0, p1y: 0, p2x: 1, p2y: 1 };
	}
	if (curve === "ease-in") {
		return { p1x: 0.42, p1y: 0, p2x: 1, p2y: 1 };
	}
	if (curve === "ease-out") {
		return { p1x: 0, p1y: 0, p2x: 0.58, p2y: 1 };
	}
	if (curve === "ease-in-out") {
		return { p1x: 0.42, p1y: 0, p2x: 0.58, p2y: 1 };
	}
	const [p1x, p1y, p2x, p2y] = curve.bezier;
	return { p1x, p1y, p2x, p2y };
}

/** Cubic timing curve from (0,0) to (1,1); matches skewer BezierCurve. */
function sampleBezierX(t: number, p1x: number, p2x: number): number {
	const u = 1 - t;
	return 3 * u * u * t * p1x + 3 * u * t * t * p2x + t * t * t;
}

function sampleBezierY(t: number, p1y: number, p2y: number): number {
	const u = 1 - t;
	return 3 * u * u * t * p1y + 3 * u * t * t * p2y + t * t * t;
}

function sampleBezierDX(t: number, p1x: number, p2x: number): number {
	const u = 1 - t;
	const term1 = p1x * 3 * (u * u - 2 * u * t);
	const term2 = p2x * 3 * (-t * t + 2 * u * t);
	return term1 + term2 + 3 * t * t;
}

function solveBezierParameter(
	u: number,
	p1x: number,
	p1y: number,
	p2x: number,
	p2y: number,
): number {
	if (u <= 0) return 0;
	if (u >= 1) return 1;

	let t = u;
	for (let i = 0; i < BEZIER_NEWTON_MAX_ITER; i++) {
		const x = sampleBezierX(t, p1x, p2x);
		const dx = sampleBezierDX(t, p1x, p2x);
		const f = x - u;
		if (Math.abs(f) < BEZIER_NEWTON_EPS) break;
		if (Math.abs(dx) < 1e-8) break;
		t -= f / dx;
		t = Math.min(1, Math.max(0, t));
	}
	t = Math.min(1, Math.max(0, t));
	return sampleBezierY(t, p1y, p2y);
}

/** Easing alpha in [0,1] for segment progress u in [0,1], using k1's curve (k0 → k1). */
export function evaluateBezierEasing(
	curve: InterpCurve | undefined,
	u: number,
): number {
	const { p1x, p1y, p2x, p2y } = curveToBezierControlPoints(curve);
	const clamped = Math.min(1, Math.max(0, u));
	return solveBezierParameter(clamped, p1x, p1y, p2x, p2y);
}

function buildResolvedKeyframes(keyframes: Keyframe[]): ResolvedKeyframe[] {
	/** Accumulate in time order (well-formed scenes); avoids odd JSON key orders. */
	const sortedKfs = [...keyframes].sort((a, b) => a.time - b.time);
	let curT: Vec3 = [0, 0, 0];
	let curR: Vec3 = [0, 0, 0];
	let curS: Vec3 = [1, 1, 1];
	const out: ResolvedKeyframe[] = [];

	for (const kf of sortedKfs) {
		if (kf.translate) curT = [...kf.translate] as Vec3;
		if (kf.rotate) curR = [...kf.rotate] as Vec3;
		if (kf.scale !== undefined) {
			if (typeof kf.scale === "number") {
				curS = [kf.scale, kf.scale, kf.scale];
			} else {
				curS = [...kf.scale] as Vec3;
			}
		}
		out.push({
			time: kf.time,
			translate: [...curT] as Vec3,
			rotate: [...curR] as Vec3,
			scale: [...curS] as Vec3,
			curve: kf.curve,
		});
	}

	return out;
}

function resolvedToStatic(k: ResolvedKeyframe): StaticTransform {
	const out: StaticTransform = {
		translate: [...k.translate] as Vec3,
		rotate: [...k.rotate] as Vec3,
	};
	const sx = k.scale[0];
	const sy = k.scale[1];
	const sz = k.scale[2];
	if (Math.abs(sx - sy) < 1e-6 && Math.abs(sy - sz) < 1e-6) {
		out.scale = sx;
	} else {
		out.scale = [...k.scale] as Vec3;
	}
	return out;
}

function interpolateResolved(
	k0: ResolvedKeyframe,
	k1: ResolvedKeyframe,
	alpha: number,
): StaticTransform {
	const t = lerpVec3(k0.translate, k1.translate, alpha);
	const q0 = eulerDegToQuat(k0.rotate);
	const q1 = eulerDegToQuat(k1.rotate);
	const q = q0.clone().slerp(q1, alpha);
	const r = quatToEulerDeg(q);
	const sv = lerpVec3(k0.scale, k1.scale, alpha);
	let scale: StaticTransform["scale"];
	if (Math.abs(sv[0] - sv[1]) < 1e-6 && Math.abs(sv[1] - sv[2]) < 1e-6) {
		scale = sv[0];
	} else {
		scale = [...sv] as Vec3;
	}
	return { translate: t, rotate: r, scale };
}

/** Evaluate transform at time `time` (seconds). Matches skewer AnimatedTransform::Evaluate. */
export function evaluateTransformAt(
	t: NodeTransform | undefined,
	time: number,
): StaticTransform {
	if (t === undefined) return {};
	if (!isAnimated(t)) return { ...t };

	const kf = t.keyframes;
	if (kf.length === 0) return {};

	const sorted = buildResolvedKeyframes(kf);
	if (sorted.length === 1) {
		return resolvedToStatic(sorted[0]);
	}

	const first = sorted[0];
	const last = sorted[sorted.length - 1];
	if (time <= first.time) return resolvedToStatic(first);
	if (time >= last.time) return resolvedToStatic(last);

	const hi = sorted.findIndex((k) => k.time > time);
	const i = hi - 1;
	const k0 = sorted[i];
	const k1 = sorted[i + 1];
	const dt = k1.time - k0.time;
	if (dt <= 1e-20) return resolvedToStatic(k1);

	let localU = (time - k0.time) / dt;
	localU = Math.min(1, Math.max(0, localU));
	const alpha = evaluateBezierEasing(k1.curve, localU);
	return interpolateResolved(k0, k1, alpha);
}

const KEYFRAME_TIME_MATCH_EPS = 1e-4;

function sortKeyframesByTime(kfs: Keyframe[]): Keyframe[] {
	return [...kfs].sort((a, b) => a.time - b.time);
}

/**
 * Add or update the keyframe at `time` with TRS from a gizmo (or other) static edit.
 * Preserves `curve` (and other fields) on an existing key at this time. New keys
 * get `curve: "linear"`.
 */
export function applyStaticTransformToAnimatedAtTime(
	anim: AnimatedTransform,
	time: number,
	trs: StaticTransform,
): AnimatedTransform {
	const keyframes = [...anim.keyframes];
	const j = keyframes.findIndex(
		(k) => Math.abs(k.time - time) < KEYFRAME_TIME_MATCH_EPS,
	);
	if (j >= 0) {
		const old = keyframes[j];
		keyframes[j] = {
			...old,
			translate: trs.translate ?? [0, 0, 0],
			rotate: trs.rotate ?? [0, 0, 0],
			scale: trs.scale,
		};
	} else {
		keyframes.push({
			time,
			translate: trs.translate,
			rotate: trs.rotate,
			scale: trs.scale,
			curve: "linear",
		});
	}
	return { keyframes: sortKeyframesByTime(keyframes) };
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

export function visitSceneNodes(
	nodes: SceneNode[],
	fn: (n: SceneNode) => void,
) {
	for (const n of nodes) {
		fn(n);
		if (n.kind === "group") visitSceneNodes(n.children, fn);
	}
}

/** Min/max keyframe time across all animated nodes; {0,0} if none. */
export function getAnimationRange(scene: ResolvedScene): {
	start: number;
	end: number;
} {
	let minT = Number.POSITIVE_INFINITY;
	let maxT = Number.NEGATIVE_INFINITY;
	let any = false;

	for (const layer of [...scene.contexts, ...scene.layers]) {
		visitSceneNodes(layer.data.graph, (node) => {
			const tr = node.transform;
			if (!isAnimated(tr)) return;
			for (const k of tr.keyframes) {
				any = true;
				if (k.time < minT) minT = k.time;
				if (k.time > maxT) maxT = k.time;
			}
		});
	}

	if (!any) return { start: 0, end: 0 };
	return { start: minT, end: maxT };
}

/** Unique keyframe times from every animated node (timeline markers). */
export function collectSceneKeyframeTimes(scene: ResolvedScene): number[] {
	const times = new Set<number>();
	for (const layer of [...scene.contexts, ...scene.layers]) {
		visitSceneNodes(layer.data.graph, (node) => {
			const tr = node.transform;
			if (!isAnimated(tr)) return;
			for (const k of tr.keyframes) times.add(k.time);
		});
	}
	return [...times].sort((a, b) => a - b);
}

/** One row in the dope-sheet: one animated scene node with its raw keyframes. */
export interface AnimatedNodeTrack {
	/** Stable identity key, same format as used throughout the app. */
	key: string;
	label: string;
	kind: SceneNode["kind"];
	layerTag: "ctx" | "lyr";
	layerIdx: number;
	layerName: string;
	/** Nesting depth for visual indentation (root nodes = 0). */
	depth: number;
	/** Path keys of every ancestor group, root-first. Used for tree-expand logic. */
	ancestorKeys: string[];
	/** Raw keyframes — future per-property graph editor reads translate/rotate/scale/curve. */
	keyframes: Keyframe[];
}

/**
 * Collect one AnimatedNodeTrack per animated node, DFS pre-order (contexts → layers).
 * Also emits group-ancestor tracks (keyframes=[]) so the dope-sheet can render tree rows
 * for groups that contain animated descendants even if they are not animated themselves.
 */
export function collectAnimatedNodeTracks(
	scene: ResolvedScene,
): AnimatedNodeTrack[] {
	const tracks: AnimatedNodeTrack[] = [];

	function walk(
		nodes: SceneNode[],
		tag: "ctx" | "lyr",
		layerIdx: number,
		layerName: string,
		indices: number[],
		ancestorKeys: string[],
	): boolean {
		let subtreeHasAnimation = false;
		for (let i = 0; i < nodes.length; i++) {
			const node = nodes[i];
			const nodeIndices = [...indices, i];
			const key = formatObjectPathKey(tag, layerIdx, nodeIndices);

			if (node.kind === "group") {
				// Speculatively record the insertion point; fill in after we know if
				// any descendant is animated (we don't want empty group rows).
				const insertIdx = tracks.length;
				const groupAncestors = [...ancestorKeys, key];
				const childHasAnim = walk(
					node.children,
					tag,
					layerIdx,
					layerName,
					nodeIndices,
					groupAncestors,
				);
				if (childHasAnim || isAnimated(node.transform)) {
					subtreeHasAnimation = true;
					// Insert the group row BEFORE its children (pre-order).
					tracks.splice(insertIdx, 0, {
						key,
						label: displayLabel(node, key),
						kind: node.kind,
						layerTag: tag,
						layerIdx,
						layerName,
						depth: indices.length,
						ancestorKeys,
						keyframes: isAnimated(node.transform)
							? node.transform.keyframes
							: [],
					});
				}
			} else {
				if (isAnimated(node.transform)) {
					subtreeHasAnimation = true;
					tracks.push({
						key,
						label: displayLabel(node, key),
						kind: node.kind,
						layerTag: tag,
						layerIdx,
						layerName,
						depth: indices.length,
						ancestorKeys,
						keyframes: node.transform.keyframes,
					});
				}
			}
		}
		return subtreeHasAnimation;
	}

	for (let i = 0; i < scene.contexts.length; i++) {
		const layer = scene.contexts[i];
		walk(layer.data.graph, "ctx", i, layer.name, [], []);
	}
	for (let i = 0; i < scene.layers.length; i++) {
		const layer = scene.layers[i];
		walk(layer.data.graph, "lyr", i, layer.name, [], []);
	}

	return tracks;
}
