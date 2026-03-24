import { useState } from "react";
import { openSceneFolder } from "../services/fs";
import { loadScene } from "../services/scene-parser";
import type { ResolvedScene } from "../types/scene";

interface Props {
	onSceneLoaded: (scene: ResolvedScene) => void;
	onError: (error: string) => void;
}

export function OpenFolderButton({ onSceneLoaded, onError }: Props) {
	const [loading, setLoading] = useState(false);

	async function handleClick() {
		setLoading(true);
		try {
			const dir = await openSceneFolder();
			const scene = await loadScene(dir);
			onSceneLoaded(scene);
			onError("");
		} catch (err) {
			// DOMException with name "AbortError" means the user cancelled the picker — ignore silently.
			if (err instanceof DOMException && err.name === "AbortError") return;
			onError(err instanceof Error ? err.message : String(err));
		} finally {
			setLoading(false);
		}
	}

	return (
		<button type="button" onClick={handleClick} disabled={loading}>
			{loading ? "Loading…" : "Open Scene Folder"}
		</button>
	);
}
