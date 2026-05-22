import * as THREE from "three";
import type {
	AnimatedTransform,
	Camera,
	CameraKeyframe,
	InterpCurve,
	Keyframe,
	NodeTransform,
	ResolvedScene,
	SceneNode,
	StaticTransform,
	Vec3,
} from "../types/scene";
import { isAnimated } from "../types/scene";
import { formatObjectPathKey } from "./graph-path";
import { displayLabel } from "./node-labels";

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

interface ResolvedCameraKeyframe {
	time: number;
	look_from: Vec3;
	look_at: Vec3;
	vup: Vec3;
	vfov: number;
	aperture_radius: number;
	focus_distance: number;
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
function evaluateBezierEasing(
	curve: InterpCurve | undefined,
	u: number,
): number {
	const { p1x, p1y, p2x, p2y } = curveToBezierControlPoints(curve);
	const clamped = Math.min(1, Math.max(0, u));
	return solveBezierParameter(clamped, p1x, p1y, p2x, p2y);
}

function buildResolvedKeyframes(keyframes: Keyframe[]): ResolvedKeyframe[] {
	/** Accumulate in time order (well-formed scenes); avoids odd JSON key orders. */
	const sortedKfs = keyframes.toSorted((a, b) => a.time - b.time);
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

function buildResolvedCameraKeyframes(
	camera: Camera,
	keyframes: CameraKeyframe[],
): ResolvedCameraKeyframe[] {
	const sortedKfs = keyframes.toSorted((a, b) => a.time - b.time);
	let lookFrom: Vec3 = [...camera.look_from] as Vec3;
	let lookAt: Vec3 = [...camera.look_at] as Vec3;
	let vup: Vec3 = [...camera.vup] as Vec3;
	let vfov = camera.vfov;
	let apertureRadius = camera.aperture_radius;
	let focusDistance = camera.focus_distance;
	const out: ResolvedCameraKeyframe[] = [];

	for (const kf of sortedKfs) {
		if (kf.look_from) lookFrom = [...kf.look_from] as Vec3;
		if (kf.look_at) lookAt = [...kf.look_at] as Vec3;
		if (kf.vup) vup = [...kf.vup] as Vec3;
		if (kf.vfov !== undefined) vfov = kf.vfov;
		if (kf.aperture_radius !== undefined) apertureRadius = kf.aperture_radius;
		if (kf.focus_distance !== undefined) focusDistance = kf.focus_distance;
		out.push({
			time: kf.time,
			look_from: [...lookFrom] as Vec3,
			look_at: [...lookAt] as Vec3,
			vup: [...vup] as Vec3,
			vfov,
			aperture_radius: apertureRadius,
			focus_distance: focusDistance,
			curve: kf.curve,
		});
	}

	return out;
}

const resolvedCameraKeyframesCache = new WeakMap<
	CameraKeyframe[],
	ResolvedCameraKeyframe[]
>();

function getResolvedCameraKeyframes(
	camera: Camera,
	keyframes: CameraKeyframe[],
): ResolvedCameraKeyframe[] {
	const cached = resolvedCameraKeyframesCache.get(keyframes);
	if (cached) return cached;
	const resolved = buildResolvedCameraKeyframes(camera, keyframes);
	resolvedCameraKeyframesCache.set(keyframes, resolved);
	return resolved;
}

function cameraFromResolved(
	camera: Camera,
	resolved: ResolvedCameraKeyframe,
	keyframes: CameraKeyframe[],
): Camera {
	return {
		...camera,
		look_from: [...resolved.look_from] as Vec3,
		look_at: [...resolved.look_at] as Vec3,
		vup: [...resolved.vup] as Vec3,
		vfov: resolved.vfov,
		aperture_radius: resolved.aperture_radius,
		focus_distance: resolved.focus_distance,
		keyframes,
	};
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

export function cameraHasKeyframes(camera: Camera): boolean {
	return (camera.keyframes?.length ?? 0) > 0;
}

export function evaluateCameraAt(camera: Camera, time: number): Camera {
	const keyframes = camera.keyframes;
	if (!keyframes || keyframes.length === 0) {
		return { ...camera };
	}

	const sorted = getResolvedCameraKeyframes(camera, keyframes);
	if (sorted.length === 1) {
		return cameraFromResolved(camera, sorted[0], keyframes);
	}

	const first = sorted[0];
	const last = sorted[sorted.length - 1];
	if (time <= first.time) {
		return cameraFromResolved(camera, first, keyframes);
	}
	if (time >= last.time) {
		return cameraFromResolved(camera, last, keyframes);
	}

	const hi = sorted.findIndex((k) => k.time > time);
	const i = hi - 1;
	const k0 = sorted[i];
	const k1 = sorted[i + 1];
	const dt = k1.time - k0.time;
	if (dt <= 1e-20) {
		return cameraFromResolved(camera, k1, keyframes);
	}

	const localU = Math.min(1, Math.max(0, (time - k0.time) / dt));
	const alpha = evaluateBezierEasing(k1.curve, localU);
	return {
		...camera,
		look_from: lerpVec3(k0.look_from, k1.look_from, alpha),
		look_at: lerpVec3(k0.look_at, k1.look_at, alpha),
		vup: lerpVec3(k0.vup, k1.vup, alpha),
		vfov: k0.vfov + (k1.vfov - k0.vfov) * alpha,
		aperture_radius:
			k0.aperture_radius + (k1.aperture_radius - k0.aperture_radius) * alpha,
		focus_distance:
			k0.focus_distance + (k1.focus_distance - k0.focus_distance) * alpha,
		keyframes,
	};
}

const KEYFRAME_TIME_MATCH_EPS = 1e-4;

function sortKeyframesByTime(kfs: Keyframe[]): Keyframe[] {
	return kfs.toSorted((a, b) => a.time - b.time);
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

/** True iff any node in the layer's graph has an AnimatedTransform with keyframes. */
export function layerHasKeyframes(graph: SceneNode[]): boolean {
	let found = false;
	visitSceneNodes(graph, (node) => {
		if (found) return;
		const tr = node.transform;
		if (isAnimated(tr) && tr.keyframes.length > 0) found = true;
	});
	return found;
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
	return Array.from(times).toSorted((a, b) => a - b);
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
