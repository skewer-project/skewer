import type { ResolvedScene } from "../types/scene";
import { getFile, readTextFile } from "./fs";
import {
	formatJsonLine,
	serializeLayerData,
	serializeSceneJSON,
} from "./scene-serializer";
import { visitSceneNodes } from "./transform";

export interface BundleFile {
	path: string;
	blob: Blob;
	contentType: string;
}

const BUNDLE_READ_CONCURRENCY = 8;

const MTL_TEX_LINE =
	/^(map_(?:kd|ks|ka|ns|d|bump)|bump|norm|disp|refl)\s+(.+)$/i;

async function mapWithConcurrency<T, R>(
	items: T[],
	limit: number,
	fn: (item: T) => Promise<R>,
): Promise<R[]> {
	const results = new Array<R>(items.length);
	const cap = Math.max(1, Math.min(limit, items.length));
	let next = 0;

	const runNext = async (): Promise<void> => {
		const index = next++;
		if (index >= items.length) return;
		results[index] = await fn(items[index] as T);
		await runNext();
	};

	await Promise.all(Array.from({ length: cap }, () => runNext()));
	return results;
}

function pushUniquePath(
	paths: string[],
	seen: Set<string>,
	path: string,
): void {
	if (seen.has(path)) return;
	seen.add(path);
	paths.push(path);
}

function assertSafeRelativePath(p: string): void {
	if (p.startsWith("/")) {
		throw new Error(`Path must be relative (no leading slash): ${p}`);
	}
	for (const seg of p.split("/")) {
		if (seg === "..") {
			throw new Error(`Path must not contain "..": ${p}`);
		}
	}
}

function parseMtllibNames(objText: string): string[] {
	const names: string[] = [];
	for (const line of objText.split("\n")) {
		const match = line.trim().match(/^mtllib\s+(.+)$/);
		if (match) names.push(match[1].trim());
	}
	return names;
}

function mtlMapPath(remainder: string): string {
	const parts = remainder.trim().split(/\s+/);
	for (let i = parts.length - 1; i >= 0; i--) {
		const t = parts[i]?.replace(/\\/g, "/");
		if (t.includes(".") && !t.startsWith("-")) {
			return t;
		}
	}
	if (parts.length) {
		return parts[parts.length - 1]?.replace(/\\/g, "/");
	}
	return remainder.trim();
}

function parseMtlTexturePaths(mtlText: string): string[] {
	const paths: string[] = [];
	for (const line of mtlText.split("\n")) {
		const match = line.trim().match(MTL_TEX_LINE);
		if (match) paths.push(mtlMapPath(match[2] ?? ""));
	}
	return paths;
}

function contentTypeForPath(filePath: string): string {
	const lower = filePath.toLowerCase();
	if (lower.endsWith(".png")) return "image/png";
	if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
	if (lower.endsWith(".bmp")) return "image/bmp";
	if (lower.endsWith(".exr")) return "image/x-exr";
	if (lower.endsWith(".hdr")) return "image/vnd.radiance";
	if (lower.endsWith(".tga")) return "image/x-tga";
	return "application/octet-stream";
}

function objBaseDir(file: string): string {
	return file.includes("/") ? `${file.split("/").slice(0, -1).join("/")}/` : "";
}

function collectMaterialTexturePaths(
	scene: ResolvedScene,
	existingPaths: Set<string>,
): string[] {
	const paths: string[] = [];
	for (const l of [...scene.contexts, ...scene.layers]) {
		for (const material of Object.values(l.data.materials)) {
			if (material.albedo_texture) {
				pushUniquePath(paths, existingPaths, material.albedo_texture);
			}
			if (material.normal_texture) {
				pushUniquePath(paths, existingPaths, material.normal_texture);
			}
			if (material.roughness_texture) {
				pushUniquePath(paths, existingPaths, material.roughness_texture);
			}
		}
	}
	return paths;
}

function collectMediumPaths(
	scene: ResolvedScene,
	existingPaths: Set<string>,
): string[] {
	const paths: string[] = [];
	for (const l of [...scene.contexts, ...scene.layers]) {
		for (const medium of Object.values(l.data.media ?? {})) {
			if (medium.file) {
				pushUniquePath(paths, existingPaths, medium.file);
			}
		}
	}
	return paths;
}

function collectSkyboxTexturePaths(
	scene: ResolvedScene,
	existingPaths: Set<string>,
): string[] {
	const paths: string[] = [];
	if (!scene.skybox) return paths;

	for (const path of Object.values(scene.skybox.faces)) {
		if (path) {
			pushUniquePath(paths, existingPaths, path);
		}
	}
	return paths;
}

function uniqueObjFiles(scene: ResolvedScene): string[] {
	const set = new Set<string>();
	for (const l of [...scene.contexts, ...scene.layers]) {
		visitSceneNodes(l.data.graph, (n) => {
			if (n.kind === "obj") set.add(n.file);
		});
	}
	return [...set];
}

interface ObjText {
	path: string;
	text: string;
}

interface MtlText {
	path: string;
	text: string;
	textureBaseDir: string;
}

async function readRequiredBlob(
	dir: FileSystemDirectoryHandle,
	path: string,
	missingLabel: string,
): Promise<BundleFile> {
	assertSafeRelativePath(path);
	try {
		const blob = await getFile(dir, path);
		return {
			path,
			blob,
			contentType: contentTypeForPath(path),
		};
	} catch (e) {
		throw new Error(
			`Missing ${missingLabel} for bundle: ${path} (${e instanceof Error ? e.message : String(e)})`,
		);
	}
}

async function readObjText(
	dir: FileSystemDirectoryHandle,
	path: string,
): Promise<ObjText> {
	assertSafeRelativePath(path);
	try {
		return { path, text: await readTextFile(dir, path) };
	} catch (e) {
		throw new Error(
			`Failed to read OBJ for bundle: ${path} (${e instanceof Error ? e.message : String(e)})`,
		);
	}
}

async function readMtlText(
	dir: FileSystemDirectoryHandle,
	spec: { path: string; textureBaseDir: string },
): Promise<MtlText | null> {
	assertSafeRelativePath(spec.path);
	try {
		return {
			path: spec.path,
			text: await readTextFile(dir, spec.path),
			textureBaseDir: spec.textureBaseDir,
		};
	} catch {
		console.warn(`[collectSceneBundle] MTL not found (skipping): ${spec.path}`);
		return null;
	}
}

async function readOptionalTexture(
	dir: FileSystemDirectoryHandle,
	path: string,
): Promise<BundleFile | null> {
	assertSafeRelativePath(path);
	try {
		const blob = await getFile(dir, path);
		return {
			path,
			blob,
			contentType: contentTypeForPath(path),
		};
	} catch {
		console.warn(`[collectSceneBundle] texture not found: ${path}`);
		return null;
	}
}

export async function collectSceneBundle(
	scene: ResolvedScene,
	dir: FileSystemDirectoryHandle,
): Promise<BundleFile[]> {
	const files = new Map<string, BundleFile>();

	files.set("scene.json", {
		path: "scene.json",
		blob: new Blob([serializeSceneJSON(scene)], { type: "application/json" }),
		contentType: "application/json",
	});

	for (const l of scene.contexts) {
		files.set(l.path, {
			path: l.path,
			blob: new Blob([formatJsonLine(serializeLayerData(l.data))], {
				type: "application/json",
			}),
			contentType: "application/json",
		});
	}
	for (const l of scene.layers) {
		files.set(l.path, {
			path: l.path,
			blob: new Blob([formatJsonLine(serializeLayerData(l.data))], {
				type: "application/json",
			}),
			contentType: "application/json",
		});
	}

	const seenPaths = new Set(files.keys());
	const materialTexturePaths = collectMaterialTexturePaths(scene, seenPaths);
	const materialTextures = await mapWithConcurrency(
		materialTexturePaths,
		BUNDLE_READ_CONCURRENCY,
		(path) => readRequiredBlob(dir, path, "material texture"),
	);
	for (const file of materialTextures) {
		files.set(file.path, file);
	}

	const skyboxTexturePaths = collectSkyboxTexturePaths(scene, seenPaths);
	const skyboxTextures = await mapWithConcurrency(
		skyboxTexturePaths,
		BUNDLE_READ_CONCURRENCY,
		(path) => readRequiredBlob(dir, path, "skybox texture"),
	);
	for (const file of skyboxTextures) {
		files.set(file.path, file);
	}

	const mediumPaths = collectMediumPaths(scene, seenPaths);
	const mediumFiles = await mapWithConcurrency(
		mediumPaths,
		BUNDLE_READ_CONCURRENCY,
		(path) => readRequiredBlob(dir, path, "medium file"),
	);
	for (const file of mediumFiles) {
		files.set(file.path, file);
	}

	const objFiles = uniqueObjFiles(scene).filter((path) => !files.has(path));
	const objTexts = await mapWithConcurrency(
		objFiles,
		BUNDLE_READ_CONCURRENCY,
		(path) => readObjText(dir, path),
	);
	for (const obj of objTexts) {
		files.set(obj.path, {
			path: obj.path,
			blob: new Blob([obj.text], { type: "model/obj" }),
			contentType: "model/obj",
		});
	}

	const mtlSeen = new Set(files.keys());
	const mtlSpecs: { path: string; textureBaseDir: string }[] = [];
	for (const obj of objTexts) {
		const baseDir = objBaseDir(obj.path);
		for (const mtlName of parseMtllibNames(obj.text)) {
			const path = baseDir + mtlName;
			if (mtlSeen.has(path)) continue;
			mtlSeen.add(path);
			mtlSpecs.push({ path, textureBaseDir: baseDir });
		}
	}

	const mtlTexts = (
		await mapWithConcurrency(mtlSpecs, BUNDLE_READ_CONCURRENCY, (spec) =>
			readMtlText(dir, spec),
		)
	).filter((mtl): mtl is MtlText => mtl !== null);
	for (const mtl of mtlTexts) {
		files.set(mtl.path, {
			path: mtl.path,
			blob: new Blob([mtl.text], { type: "text/plain" }),
			contentType: "text/plain",
		});
	}

	const textureSeen = new Set(files.keys());
	const texturePaths: string[] = [];
	for (const mtl of mtlTexts) {
		for (const rel of parseMtlTexturePaths(mtl.text)) {
			pushUniquePath(texturePaths, textureSeen, mtl.textureBaseDir + rel);
		}
	}

	const textureFiles = (
		await mapWithConcurrency(texturePaths, BUNDLE_READ_CONCURRENCY, (path) =>
			readOptionalTexture(dir, path),
		)
	).filter((file): file is BundleFile => file !== null);
	for (const file of textureFiles) {
		files.set(file.path, file);
	}

	return Array.from(files.values());
}
