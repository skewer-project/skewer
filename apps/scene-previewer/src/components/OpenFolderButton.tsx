import { useState } from "react";
import { openSceneFolder } from "../services/fs";
import { loadScene } from "../services/scene-parser";
import u from "../styles/shared/uiPrimitives.module.css";
import type { ResolvedScene } from "../types/scene";

interface Props {
	onSceneLoaded: (scene: ResolvedScene, dir: FileSystemDirectoryHandle) => void;
	onError: (error: string) => void;
}

export function OpenFolderButton({ onSceneLoaded, onError }: Props) {
	const [loading, setLoading] = useState(false);

	async function handleClick() {
		setLoading(true);
		try {
			const dir = await openSceneFolder();
			const scene = await loadScene(dir);
			onSceneLoaded(scene, dir);
		} catch (err) {
			if (err instanceof DOMException && err.name === "AbortError") return;
			onError(err instanceof Error ? err.message : String(err));
		} finally {
			setLoading(false);
		}
	}

	return (
		<button
			type="button"
			onClick={handleClick}
			disabled={loading}
			className={`${u.openBtn}${loading ? " loading" : ""}`}
		>
			{loading ? "Loading…" : "Open Scene Folder"}
		</button>
	);
}
