import { useState } from "react";
import { OpenFolderButton } from "./components/OpenFolderButton";
import { SceneInspector } from "./components/SceneInspector";
import { Viewport } from "./components/Viewport";
import type { ResolvedScene } from "./types/scene";

function App() {
	const [scene, setScene] = useState<ResolvedScene | null>(null);
	const [dirHandle, setDirHandle] = useState<FileSystemDirectoryHandle | null>(null);
	const [error, setError] = useState<string>("");

	function handleSceneLoaded(s: ResolvedScene, dir: FileSystemDirectoryHandle) {
		setScene(s);
		setDirHandle(dir);
		setError("");
	}

	const totalObjects = scene
		? [...scene.contexts, ...scene.layers].reduce((s, l) => s + l.data.objects.length, 0)
		: 0;

	return (
		<div className="app-root">
			{/* Full-screen viewport */}
			<div className="viewport-fill">
				<Viewport scene={scene} dirHandle={dirHandle} />
			</div>

			{/* HUD overlay */}
			<div className="hud">
				<div className="vignette" aria-hidden="true" />

				{/* Top-left: header panel */}
				<div className="panel hud-header">
					<span className="wordmark">Skewer</span>
					<OpenFolderButton onSceneLoaded={handleSceneLoaded} onError={setError} />
					{error && <span className="error-msg">{error}</span>}
				</div>

				{/* Left sidebar: scene inspector */}
				{scene && (
					<div className="panel hud-sidebar">
						<SceneInspector scene={scene} />
					</div>
				)}

				{/* Bottom-right: stats */}
				{scene && (
					<div className="panel hud-stats">
						<span className="stat-tag stat-ctx">{scene.contexts.length}c</span>
						<span className="stat-sep">/</span>
						<span className="stat-tag stat-lyr">{scene.layers.length}L</span>
						<span className="stat-sep">/</span>
						<span className="stat-num">{totalObjects} obj</span>
						{scene.output_dir && (
							<>
								<span className="stat-sep">→</span>
								<span className="stat-dir">{scene.output_dir}</span>
							</>
						)}
					</div>
				)}

				{/* Empty state */}
				{!scene && (
					<div className="empty-state">
						<div className="empty-wordmark">Skewer</div>
						<div className="empty-sub">Spectral Scene Previewer</div>
						<div className="empty-hint">open a scene folder above to begin</div>
					</div>
				)}
			</div>
		</div>
	);
}

export default App;
