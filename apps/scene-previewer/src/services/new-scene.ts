// Creates a new scene from the bundled template assets.
// Fetches template JSON files, writes them to a user-selected directory,
// and returns a parsed ResolvedScene — no read-after-write needed.

import type { ResolvedScene } from "../types/scene";
import { writeFile } from "./fs";
import { parseLayerData, parseSceneManifest } from "./scene-parser";

const TEMPLATE_FILES = [
	"scene.json",
	"ctx_lighting.json",
	"layer_env.json",
	"layer_sphere_gold.json",
	"layer_sphere_blue.json",
] as const;

export async function createNewScene(
	dir: FileSystemDirectoryHandle,
): Promise<ResolvedScene> {
	// Fetch all template files in parallel
	const texts = await Promise.all(
		TEMPLATE_FILES.map((f) =>
			fetch(`/templates/${f}`).then((r) => {
				if (!r.ok) throw new Error(`Failed to fetch template: ${f}`);
				return r.text();
			}),
		),
	);

	// Write to disk in parallel
	await Promise.all(
		TEMPLATE_FILES.map((f, i) => writeFile(dir, f, texts[i])),
	);

	// Parse directly from fetched content — no read-after-write
	const [sceneRaw, ctxRaw, envRaw, goldRaw, blueRaw] = texts.map((t) =>
		JSON.parse(t),
	);

	const manifest = parseSceneManifest(sceneRaw);

	return {
		camera: manifest.camera,
		contexts: [
			{
				name: "ctx_lighting",
				path: "ctx_lighting.json",
				data: parseLayerData(ctxRaw),
			},
		],
		layers: [
			{
				name: "layer_env",
				path: "layer_env.json",
				data: parseLayerData(envRaw),
			},
			{
				name: "layer_sphere_gold",
				path: "layer_sphere_gold.json",
				data: parseLayerData(goldRaw),
			},
			{
				name: "layer_sphere_blue",
				path: "layer_sphere_blue.json",
				data: parseLayerData(blueRaw),
			},
		],
		output_dir: manifest.output_dir,
	};
}
