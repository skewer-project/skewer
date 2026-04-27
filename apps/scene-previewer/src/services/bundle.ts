import type { Material, ResolvedScene } from "../types/scene";
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

const MTL_TEX_LINE =
	/^(map_(?:kd|ks|ka|ns|d|bump)|bump|norm|disp|refl)\s+(.+)$/i;

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
		const t = parts[i]!.replace(/\\/g, "/");
		if (t.includes(".") && !t.startsWith("-")) {
			return t;
		}
	}
	if (parts.length) {
		return parts[parts.length - 1]!.replace(/\\/g, "/");
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

async function addMaterialTextures(
	dir: FileSystemDirectoryHandle,
	material: Material,
	files: Map<string, BundleFile>,
) {
	const rels: string[] = [];
	if (material.albedo_texture) rels.push(material.albedo_texture);
	if (material.normal_texture) rels.push(material.normal_texture);
	if (material.roughness_texture) rels.push(material.roughness_texture);
	for (const p of rels) {
		if (files.has(p)) continue;
		assertSafeRelativePath(p);
		try {
			const blob = await getFile(dir, p);
			files.set(p, {
				path: p,
				blob,
				contentType: contentTypeForPath(p),
			});
		} catch (e) {
			throw new Error(
				`Missing material texture for bundle: ${p} (${e instanceof Error ? e.message : String(e)})`,
			);
		}
	}
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

async function addObjMtlAndTextures(
	dir: FileSystemDirectoryHandle,
	objFile: string,
	files: Map<string, BundleFile>,
) {
	assertSafeRelativePath(objFile);

	let objText: string;
	try {
		objText = await readTextFile(dir, objFile);
	} catch (e) {
		throw new Error(
			`Failed to read OBJ for bundle: ${objFile} (${e instanceof Error ? e.message : String(e)})`,
		);
	}

	if (!files.has(objFile)) {
		files.set(objFile, {
			path: objFile,
			blob: new Blob([objText], { type: "model/obj" }),
			contentType: "model/obj",
		});
	}

	const ob = objBaseDir(objFile);
	for (const mtlName of parseMtllibNames(objText)) {
		const mtlPath = ob + mtlName;
		assertSafeRelativePath(mtlPath);
		if (files.has(mtlPath)) continue;
		let mtlText: string;
		try {
			mtlText = await readTextFile(dir, mtlPath);
		} catch {
			console.warn(`[collectSceneBundle] MTL not found (skipping): ${mtlPath}`);
			continue;
		}
		files.set(mtlPath, {
			path: mtlPath,
			blob: new Blob([mtlText], { type: "text/plain" }),
			contentType: "text/plain",
		});

		for (const rel of parseMtlTexturePaths(mtlText)) {
			const texPath = ob + rel;
			assertSafeRelativePath(texPath);
			if (files.has(texPath)) continue;
			try {
				const blob = await getFile(dir, texPath);
				files.set(texPath, {
					path: texPath,
					blob,
					contentType: contentTypeForPath(texPath),
				});
			} catch {
				console.warn(`[collectSceneBundle] texture not found: ${texPath}`);
			}
		}
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

	for (const l of [...scene.contexts, ...scene.layers]) {
		for (const m of Object.values(l.data.materials)) {
			await addMaterialTextures(dir, m, files);
		}
	}

	for (const ofile of uniqueObjFiles(scene)) {
		await addObjMtlAndTextures(dir, ofile, files);
	}

	return Array.from(files.values());
}
