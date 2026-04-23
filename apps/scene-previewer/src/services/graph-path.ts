// Path keys: "lyr:0:2/3/1" → layer 0, indices [2,3,1] into nested graph.

import type {
	AnimatedTransform,
	LayerData,
	ResolvedLayer,
	ResolvedScene,
	SceneNode,
} from "../types/scene";
import { isAnimated } from "../types/scene";

export interface ParsedObjectPath {
	tag: "ctx" | "lyr";
	layerIdx: number;
	/** Indices from root graph; empty = layer graph root (virtual parent). */
	indices: number[];
}

/** True if this is a material selection key, not an object path. */
export function isMaterialKey(key: string): boolean {
	const parts = key.split(":");
	return parts.length >= 3 && parts[2] === "mat";
}

/**
 * Parse object path key: `ctx|lyr` : layerIdx : slash-separated indices.
 * Two-part `lyr:0` = graph root parent only (for insert at top level).
 */
export function parseObjectPathKey(key: string): ParsedObjectPath | null {
	if (isMaterialKey(key)) return null;
	const parts = key.split(":");
	if (parts.length < 2) return null;
	const tag = parts[0];
	if (tag !== "ctx" && tag !== "lyr") return null;
	const layerIdx = Number(parts[1]);
	if (!Number.isInteger(layerIdx) || layerIdx < 0) return null;
	if (parts.length === 2) return { tag, layerIdx, indices: [] };
	const pathPart = parts.slice(2).join(":");
	if (pathPart === "") return { tag, layerIdx, indices: [] };
	const segments = pathPart.split("/");
	const indices: number[] = [];
	for (const seg of segments) {
		if (seg === "") return null;
		const n = Number(seg);
		if (!Number.isInteger(n) || n < 0) return null;
		indices.push(n);
	}
	return { tag, layerIdx, indices };
}

export function formatObjectPathKey(
	tag: "ctx" | "lyr",
	layerIdx: number,
	indices: number[],
): string {
	if (indices.length === 0) return `${tag}:${layerIdx}`;
	return `${tag}:${layerIdx}:${indices.join("/")}`;
}

export interface ResolvedNodeContext {
	tag: "ctx" | "lyr";
	layerIdx: number;
	layer: ResolvedLayer;
	node: SceneNode;
	/** Parent group children array, or top-level graph if parent is virtual. */
	siblings: SceneNode[];
	siblingIndex: number;
	depth: number;
	materialNames: string[];
}

/** Walk to the node at `indices`; returns null if path invalid. */
export function resolveNodeAtPath(
	scene: ResolvedScene,
	key: string,
): ResolvedNodeContext | null {
	const parsed = parseObjectPathKey(key);
	if (!parsed || parsed.indices.length === 0) return null;
	const { tag, layerIdx, indices } = parsed;
	const list = tag === "ctx" ? scene.contexts : scene.layers;
	const layer = list[layerIdx];
	if (!layer) return null;

	let siblings = layer.data.graph;
	let node: SceneNode | undefined;
	let siblingIndex = -1;

	for (let d = 0; d < indices.length; d++) {
		siblingIndex = indices[d];
		node = siblings[siblingIndex];
		if (!node) return null;
		if (d < indices.length - 1) {
			if (node.kind !== "group") return null;
			siblings = node.children;
		}
	}

	if (!node) return null;
	return {
		tag,
		layerIdx,
		layer,
		node,
		siblings,
		siblingIndex,
		depth: indices.length - 1,
		materialNames: Object.keys(layer.data.materials),
	};
}

function cloneNode(n: SceneNode): SceneNode {
	return JSON.parse(JSON.stringify(n)) as SceneNode;
}

export function updateNodeAtPath(
	scene: ResolvedScene,
	key: string,
	mutator: (node: SceneNode) => SceneNode,
): ResolvedScene {
	const parsed = parseObjectPathKey(key);
	if (!parsed || parsed.indices.length === 0) return scene;
	const { tag, layerIdx, indices } = parsed;
	const listKey = tag === "ctx" ? "contexts" : "layers";
	const newList = [...scene[listKey]];
	const layer = newList[layerIdx];
	if (!layer) return scene;

	const newLayer = {
		...layer,
		data: { ...layer.data, graph: [...layer.data.graph] },
	};
	const newGraph = newLayer.data.graph;

	function walk(arr: SceneNode[], depth: number): SceneNode[] {
		if (depth === indices.length - 1) {
			const i = indices[depth];
			const copy = [...arr];
			if (i < 0 || i >= copy.length) return arr;
			copy[i] = mutator(cloneNode(copy[i]));
			return copy;
		}
		const i = indices[depth];
		const copy = [...arr];
		if (i < 0 || i >= copy.length) return arr;
		const cur = copy[i];
		if (cur.kind !== "group") return arr;
		const nextChildren = walk(cur.children, depth + 1);
		copy[i] = { ...cur, children: nextChildren };
		return copy;
	}

	newLayer.data.graph = walk(newGraph, 0);
	newList[layerIdx] = newLayer;
	return { ...scene, [listKey]: newList };
}

export function deleteNodeAtPath(
	scene: ResolvedScene,
	key: string,
): ResolvedScene {
	const parsed = parseObjectPathKey(key);
	if (!parsed || parsed.indices.length === 0) return scene;
	const { tag, layerIdx, indices } = parsed;
	const listKey = tag === "ctx" ? "contexts" : "layers";
	const newList = [...scene[listKey]];
	const layer = newList[layerIdx];
	if (!layer) return scene;

	const newLayer = {
		...layer,
		data: { ...layer.data, graph: [...layer.data.graph] },
	};

	function walk(arr: SceneNode[], depth: number): SceneNode[] {
		if (depth === indices.length - 1) {
			const i = indices[depth];
			return arr.filter((_, j) => j !== i);
		}
		const i = indices[depth];
		const copy = [...arr];
		if (i < 0 || i >= copy.length) return arr;
		const cur = copy[i];
		if (cur.kind !== "group") return arr;
		copy[i] = { ...cur, children: walk(cur.children, depth + 1) };
		return copy;
	}

	newLayer.data.graph = walk(newLayer.data.graph, 0);
	newList[layerIdx] = newLayer;
	return { ...scene, [listKey]: newList };
}

/**
 * Insert `node` as last child of parent.
 * `parentKey` = `lyr:0` (empty indices) → append to layer graph root.
 */
export function insertChild(
	scene: ResolvedScene,
	parentKey: string,
	node: SceneNode,
): { scene: ResolvedScene; childKey: string } {
	const parsed = parseObjectPathKey(parentKey);
	if (!parsed) return { scene, childKey: parentKey };
	const { tag, layerIdx, indices } = parsed;
	const listKey = tag === "ctx" ? "contexts" : "layers";
	const newList = [...scene[listKey]];
	const layer = newList[layerIdx];
	if (!layer) return { scene, childKey: parentKey };

	const newLayer = {
		...layer,
		data: { ...layer.data, graph: [...layer.data.graph] },
	};

	if (indices.length === 0) {
		const graph = [...newLayer.data.graph];
		const childIndex = graph.length;
		graph.push(cloneNode(node));
		newLayer.data.graph = graph;
		newList[layerIdx] = newLayer;
		return {
			scene: { ...scene, [listKey]: newList },
			childKey: formatObjectPathKey(tag, layerIdx, [childIndex]),
		};
	}

	function walk(arr: SceneNode[], depth: number): SceneNode[] {
		if (depth === indices.length - 1) {
			const i = indices[depth];
			const copy = [...arr];
			if (i < 0 || i >= copy.length) return arr;
			const cur = copy[i];
			if (cur.kind !== "group") return arr;
			const children = [...cur.children, cloneNode(node)];
			copy[i] = { ...cur, children };
			return copy;
		}
		const i = indices[depth];
		const copy = [...arr];
		if (i < 0 || i >= copy.length) return arr;
		const cur = copy[i];
		if (cur.kind !== "group") return arr;
		copy[i] = { ...cur, children: walk(cur.children, depth + 1) };
		return copy;
	}

	newLayer.data.graph = walk(newLayer.data.graph, 0);
	newList[layerIdx] = newLayer;

	const childIndices = [...indices];
	const parent = resolveNodeAtPath(
		{ ...scene, [listKey]: newList },
		formatObjectPathKey(tag, layerIdx, indices),
	);
	const childIdx =
		parent && parent.node.kind === "group"
			? parent.node.children.length - 1
			: -1;
	if (childIdx < 0)
		return { scene: { ...scene, [listKey]: newList }, childKey: parentKey };
	childIndices.push(childIdx);
	return {
		scene: { ...scene, [listKey]: newList },
		childKey: formatObjectPathKey(tag, layerIdx, childIndices),
	};
}

/** All object path keys for leaves whose material matches. */
export function collectLeafKeysForMaterial(
	data: LayerData,
	tag: "ctx" | "lyr",
	layerIdx: number,
	matName: string,
): string[] {
	const keys: string[] = [];
	function visit(nodes: SceneNode[], prefix: number[]) {
		for (let i = 0; i < nodes.length; i++) {
			const n = nodes[i];
			const path = [...prefix, i];
			if (n.kind === "group") {
				visit(n.children, path);
			} else if (n.material === matName) {
				keys.push(formatObjectPathKey(tag, layerIdx, path));
			}
		}
	}
	visit(data.graph, []);
	return keys;
}

/** Recursive node count (groups + leaves). */
export function countGraphNodes(nodes: SceneNode[]): number {
	let c = 0;
	for (const n of nodes) {
		c += 1;
		if (n.kind === "group") c += countGraphNodes(n.children);
	}
	return c;
}

export interface AnimatedNodeEntry {
	objectKey: string;
	transform: AnimatedTransform;
}

function collectAnimatedInGraph(
	nodes: SceneNode[],
	tag: "ctx" | "lyr",
	layerIdx: number,
	prefix: number[],
	out: AnimatedNodeEntry[],
) {
	for (let i = 0; i < nodes.length; i++) {
		const n = nodes[i];
		const path = [...prefix, i];
		const key = formatObjectPathKey(tag, layerIdx, path);
		const tr = n.transform;
		if (tr !== undefined && isAnimated(tr)) {
			out.push({ objectKey: key, transform: tr });
		}
		if (n.kind === "group") {
			collectAnimatedInGraph(n.children, tag, layerIdx, path, out);
		}
	}
}

/** Object keys whose transforms are animated (for time-based viewport updates). */
export function collectAnimatedNodes(
	scene: ResolvedScene,
): AnimatedNodeEntry[] {
	const out: AnimatedNodeEntry[] = [];
	for (let li = 0; li < scene.contexts.length; li++) {
		collectAnimatedInGraph(scene.contexts[li].data.graph, "ctx", li, [], out);
	}
	for (let li = 0; li < scene.layers.length; li++) {
		collectAnimatedInGraph(scene.layers[li].data.graph, "lyr", li, [], out);
	}
	return out;
}
