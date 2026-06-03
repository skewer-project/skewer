import type {
	LayerData,
	ResolvedLayer,
	ResolvedScene,
	SceneNode,
} from "../types/scene";
import { DEFAULT_RENDER_CONFIG } from "../types/scene";
import { parseLayerData, parseSceneManifest } from "./scene-parser";
import { serializeLayerData, serializeSceneManifest } from "./scene-serializer";

export type SceneDeltaOperation = "update" | "add" | "delete";

export interface SceneDelta {
	operation: SceneDeltaOperation;
	filePath: string;
	jsonPath: string;
	oldValue?: unknown;
	newValue?: unknown;
}

export interface SceneHistoryEntry {
	label: string;
	deltas: SceneDelta[];
	createdAt: number;
}

export type SerializedSceneFiles = Map<string, unknown>;

const SCENE_MANIFEST_PATH = "scene.json";

function cloneJson<T>(value: T): T {
	return structuredClone(value);
}

function isObject(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isDeepEqual(a: unknown, b: unknown): boolean {
	return JSON.stringify(a) === JSON.stringify(b);
}

function isVec3Array(value: unknown): boolean {
	return (
		Array.isArray(value) &&
		value.length === 3 &&
		value.every((item) => typeof item === "number")
	);
}

function isVec3Object(value: unknown): boolean {
	return (
		isObject(value) &&
		typeof value.x === "number" &&
		typeof value.y === "number" &&
		typeof value.z === "number"
	);
}

function shouldBundleValue(a: unknown, b: unknown): boolean {
	return (
		(isVec3Array(a) && isVec3Array(b)) || (isVec3Object(a) && isVec3Object(b))
	);
}

function encodePointerSegment(segment: string): string {
	return segment.replaceAll("~", "~0").replaceAll("/", "~1");
}

function decodePointerSegment(segment: string): string {
	return segment.replaceAll("~1", "/").replaceAll("~0", "~");
}

function childPath(parent: string, segment: string | number): string {
	return `${parent}/${encodePointerSegment(String(segment))}`;
}

function pointerSegments(path: string): string[] {
	if (path === "") return [];
	if (!path.startsWith("/")) throw new Error(`Invalid JSON pointer: ${path}`);
	return path.slice(1).split("/").map(decodePointerSegment);
}

function getParentAndKey(root: unknown, path: string) {
	const segments = pointerSegments(path);
	if (segments.length === 0) return { parent: null, key: "" };
	let parent = root;
	for (const segment of segments.slice(0, -1)) {
		if (Array.isArray(parent)) {
			parent = parent[Number(segment)];
		} else if (isObject(parent)) {
			parent = parent[segment];
		} else {
			throw new Error(`Cannot resolve JSON pointer parent: ${path}`);
		}
	}
	return { parent, key: segments[segments.length - 1] };
}

function setJsonPointer(root: unknown, path: string, value: unknown): unknown {
	if (path === "") return cloneJson(value);
	const { parent, key } = getParentAndKey(root, path);
	if (Array.isArray(parent)) {
		parent[Number(key)] = cloneJson(value);
	} else if (isObject(parent)) {
		parent[key] = cloneJson(value);
	} else {
		throw new Error(`Cannot set JSON pointer: ${path}`);
	}
	return root;
}

function insertJsonPointer(
	root: unknown,
	path: string,
	value: unknown,
): unknown {
	if (path === "") return cloneJson(value);
	const { parent, key } = getParentAndKey(root, path);
	if (Array.isArray(parent)) {
		parent.splice(Number(key), 0, cloneJson(value));
	} else if (isObject(parent)) {
		parent[key] = cloneJson(value);
	} else {
		throw new Error(`Cannot insert JSON pointer: ${path}`);
	}
	return root;
}

function deleteJsonPointer(root: unknown, path: string): unknown {
	if (path === "") return undefined;
	const { parent, key } = getParentAndKey(root, path);
	if (Array.isArray(parent)) {
		parent.splice(Number(key), 1);
	} else if (isObject(parent)) {
		delete parent[key];
	} else {
		throw new Error(`Cannot delete JSON pointer: ${path}`);
	}
	return root;
}

// Prune empty objects and arrays from serialization output to reduce noise in diffs
function diffJson(
	filePath: string,
	jsonPath: string,
	oldValue: unknown,
	newValue: unknown,
	out: SceneDelta[],
) {
	if (isDeepEqual(oldValue, newValue)) return;
	// Simple case for complex values that would produce noisy diffs (vectors); treat as atomic
	if (shouldBundleValue(oldValue, newValue)) {
		out.push({
			operation: "update",
			filePath,
			jsonPath,
			oldValue: cloneJson(oldValue),
			newValue: cloneJson(newValue),
		});
		return;
	}

	// Handle array additions and deletions in singe operations to preserve element identity in diffs
	if (Array.isArray(oldValue) && Array.isArray(newValue)) {
		const prefixLength = commonPrefixLength(oldValue, newValue);
		const suffixLength = commonSuffixLength(oldValue, newValue, prefixLength);

		if (
			newValue.length === oldValue.length + 1 &&
			prefixLength + suffixLength === oldValue.length
		) {
			//
			out.push({
				operation: "add",
				filePath,
				jsonPath: childPath(jsonPath, prefixLength),
				newValue: cloneJson(newValue[prefixLength]),
			});
			return;
		}

		if (
			oldValue.length === newValue.length + 1 &&
			prefixLength + suffixLength === newValue.length
		) {
			out.push({
				operation: "delete",
				filePath,
				jsonPath: childPath(jsonPath, prefixLength),
				oldValue: cloneJson(oldValue[prefixLength]),
			});
			return;
		}

		//? Might need to check for if there is a better way for saving array diffs
		// Currently a bit messy, but I don't think we have array diffs
		const length = Math.min(oldValue.length, newValue.length);
		for (let i = 0; i < length; i++) {
			diffJson(filePath, childPath(jsonPath, i), oldValue[i], newValue[i], out);
		}
		for (let i = length; i < newValue.length; i++) {
			out.push({
				operation: "add",
				filePath,
				jsonPath: childPath(jsonPath, i),
				newValue: cloneJson(newValue[i]),
			});
		}
		for (let i = oldValue.length - 1; i >= length; i--) {
			out.push({
				operation: "delete",
				filePath,
				jsonPath: childPath(jsonPath, i),
				oldValue: cloneJson(oldValue[i]),
			});
		}
		return;
	}

	// Recurse into objects, but treat additions and deletions of entire objects
	// as atomic to preserve property identity in diffs
	if (isObject(oldValue) && isObject(newValue)) {
		const oldKeys = new Set(Object.keys(oldValue));
		const newKeys = new Set(Object.keys(newValue));
		for (const key of Object.keys(oldValue)) {
			if (!newKeys.has(key)) {
				out.push({
					operation: "delete",
					filePath,
					jsonPath: childPath(jsonPath, key),
					oldValue: cloneJson(oldValue[key]),
				});
			}
		}
		for (const key of Object.keys(newValue)) {
			if (!oldKeys.has(key)) {
				out.push({
					operation: "add",
					filePath,
					jsonPath: childPath(jsonPath, key),
					newValue: cloneJson(newValue[key]),
				});
			} else {
				diffJson(
					filePath,
					childPath(jsonPath, key),
					oldValue[key],
					newValue[key],
					out,
				);
			}
		}
		return;
	}

	out.push({
		operation: "update",
		filePath,
		jsonPath,
		oldValue: cloneJson(oldValue),
		newValue: cloneJson(newValue),
	});
}

// Array comparison helper
function commonPrefixLength(a: unknown[], b: unknown[]): number {
	const length = Math.min(a.length, b.length);
	let i = 0;
	while (i < length && isDeepEqual(a[i], b[i])) i++;
	return i;
}

// Array comparison helper
function commonSuffixLength(
	a: unknown[],
	b: unknown[],
	prefixLength: number,
): number {
	let count = 0;
	while (
		count < a.length - prefixLength &&
		count < b.length - prefixLength &&
		isDeepEqual(a[a.length - 1 - count], b[b.length - 1 - count])
	) {
		count++;
	}
	return count;
}

function stemFromPath(path: string): string {
	const filename = path.split("/").pop() ?? path;
	return filename.replace(/\.[^.]+$/, "");
}

// History serialization needs to keep runtime fields like volumetrics,
// so it cannot reuse the on-disk serialization path.
function preserveVolumetricRuntimeFields(
	sourceNode: SceneNode,
	serializedNode: unknown,
) {
	if (!isObject(serializedNode)) return;

	if (sourceNode.kind === "group") {
		const serializedChildren = serializedNode.children;
		if (!Array.isArray(serializedChildren)) return;
		for (
			let i = 0;
			i < sourceNode.children.length && i < serializedChildren.length;
			i++
		) {
			preserveVolumetricRuntimeFields(
				sourceNode.children[i],
				serializedChildren[i],
			);
		}
		return;
	}

	if (sourceNode.kind !== "sphere" || sourceNode.inside_medium === undefined) {
		return;
	}

	serializedNode.center = cloneJson(sourceNode.center);
	serializedNode.radius = sourceNode.radius;
	if (sourceNode.transform !== undefined) {
		serializedNode.transform = cloneJson(sourceNode.transform);
	}
}

function serializeLayerDataForHistory(
	data: LayerData,
): Record<string, unknown> {
	const serialized = serializeLayerData(data);
	const serializedGraph = serialized.graph;
	if (Array.isArray(serializedGraph)) {
		for (let i = 0; i < data.graph.length && i < serializedGraph.length; i++) {
			preserveVolumetricRuntimeFields(data.graph[i], serializedGraph[i]);
		}
	}
	return serialized;
}

export function serializeSceneFiles(
	scene: ResolvedScene,
): SerializedSceneFiles {
	const files: SerializedSceneFiles = new Map();
	files.set(SCENE_MANIFEST_PATH, serializeSceneManifest(scene));

	const byPath = new Map<string, ResolvedLayer>();
	for (const layer of scene.contexts) byPath.set(layer.path, layer);
	for (const layer of scene.layers) byPath.set(layer.path, layer);
	for (const [path, layer] of byPath) {
		files.set(path, serializeLayerDataForHistory(layer.data));
	}

	return files;
}

// Not currently used, but could be useful for implementing a "Save As" feature in the future
export function diffSceneFiles(
	before: SerializedSceneFiles,
	after: SerializedSceneFiles,
): SceneDelta[] {
	const out: SceneDelta[] = [];
	const paths = new Set([...before.keys(), ...after.keys()]);
	for (const filePath of [...paths].sort()) {
		const oldValue = before.get(filePath);
		const newValue = after.get(filePath);
		if (!before.has(filePath)) {
			out.push({
				operation: "add",
				filePath,
				jsonPath: "",
				newValue: cloneJson(newValue),
			});
		} else if (!after.has(filePath)) {
			out.push({
				operation: "delete",
				filePath,
				jsonPath: "",
				oldValue: cloneJson(oldValue),
			});
		} else {
			diffJson(filePath, "", oldValue, newValue, out);
		}
	}
	return out;
}

export function buildHistoryEntry(
	before: ResolvedScene,
	after: ResolvedScene,
	label: string,
): SceneHistoryEntry | null {
	const deltas = diffSceneFiles(
		serializeSceneFiles(before),
		serializeSceneFiles(after),
	);
	if (deltas.length === 0) return null;
	return { label, deltas, createdAt: Date.now() };
}

// Applied all deltas per undo history in reverse order to restore previous scene state
export function applyUndoEntry(
	scene: ResolvedScene,
	entry: SceneHistoryEntry,
): ResolvedScene {
	const files = serializeSceneFiles(scene);
	for (let i = entry.deltas.length - 1; i >= 0; i--) {
		const delta = entry.deltas[i];
		// Case undefined for when file gets added
		let root = files.has(delta.filePath)
			? cloneJson(files.get(delta.filePath))
			: undefined;

		if (delta.operation === "update") {
			root = setJsonPointer(root, delta.jsonPath, delta.oldValue);
			files.set(delta.filePath, root);
		} else if (delta.operation === "add") {
			root = deleteJsonPointer(root, delta.jsonPath);
			if (root === undefined) files.delete(delta.filePath);
			else files.set(delta.filePath, root);
		} else {
			root = insertJsonPointer(root, delta.jsonPath, delta.oldValue);
			files.set(delta.filePath, root);
		}
	}
	return hydrateSceneFiles(files, scene.settings);
}

// Apply deltas in forward order for redo — opposite of applyUndoEntry.
export function applyRedoEntry(
	scene: ResolvedScene,
	entry: SceneHistoryEntry,
): ResolvedScene {
	const files = serializeSceneFiles(scene);
	for (let i = 0; i < entry.deltas.length; i++) {
		const delta = entry.deltas[i];
		let root = files.has(delta.filePath)
			? cloneJson(files.get(delta.filePath))
			: undefined;

		if (delta.operation === "update" && delta.newValue !== undefined) {
			root = setJsonPointer(root, delta.jsonPath, delta.newValue);
			files.set(delta.filePath, root);
		} else if (delta.operation === "add" && delta.newValue !== undefined) {
			root = insertJsonPointer(root, delta.jsonPath, delta.newValue);
			files.set(delta.filePath, root);
		} else {
			root = deleteJsonPointer(root, delta.jsonPath);
			if (root === undefined) files.delete(delta.filePath);
			else files.set(delta.filePath, root);
		}
	}
	return hydrateSceneFiles(files, scene.settings);
}

// Combining differences that are not too far apart in time\
// Noise reduction
export function canCoalesceHistoryEntries(
	previous: SceneHistoryEntry,
	next: SceneHistoryEntry,
): boolean {
	if (next.createdAt - previous.createdAt > 800) return false;
	if (previous.deltas.length !== next.deltas.length) return false;
	return deltaSignature(previous) === deltaSignature(next);
}

export function coalesceHistoryEntries(
	previous: SceneHistoryEntry,
	next: SceneHistoryEntry,
): SceneHistoryEntry {
	const oldByPath = new Map(
		previous.deltas.map((delta) => [deltaKey(delta), delta] as const),
	);
	return {
		label: next.label,
		createdAt: next.createdAt,
		deltas: next.deltas.map((delta) => {
			const existing = oldByPath.get(deltaKey(delta));
			return existing
				? { ...delta, oldValue: existing.oldValue }
				: { ...delta };
		}),
	};
}

function deltaSignature(entry: SceneHistoryEntry): string {
	return entry.deltas.map(deltaKey).sort().join("|");
}

function deltaKey(delta: SceneDelta): string {
	return `${delta.operation}:${delta.filePath}:${delta.jsonPath}`;
}

// Returns either specific json
function hydrateSceneFiles(
	files: SerializedSceneFiles,
	fallbackSettings = DEFAULT_RENDER_CONFIG,
): ResolvedScene {
	const manifestJson = files.get(SCENE_MANIFEST_PATH);
	if (manifestJson === undefined) {
		throw new Error("Cannot hydrate scene history: missing scene.json");
	}
	const manifest = parseSceneManifest(manifestJson);
	const loadLayer = (path: string): ResolvedLayer => {
		const json = files.get(path);
		if (json === undefined) {
			throw new Error(`Cannot hydrate scene history: missing ${path}`);
		}
		return {
			name: stemFromPath(path),
			path,
			data: parseLayerData(json),
		};
	};
	const contexts = manifest.context.map(loadLayer);
	const layers = manifest.layers.map(loadLayer);
	const loadedRender =
		layers.find((layer) => layer.data.render != null)?.data.render ??
		contexts.find((layer) => layer.data.render != null)?.data.render;

	return {
		camera: manifest.camera,
		contexts,
		layers,
		output_dir: manifest.output_dir,
		animation: manifest.animation ?? {
			start: 0,
			end: 0,
			fps: 24,
			shutter_angle: 180,
		},
		settings: loadedRender ?? fallbackSettings,
		// Keep manifest-level data when undo/redo hydrates from serialized files.
		skybox: manifest.skybox,
	};
}
