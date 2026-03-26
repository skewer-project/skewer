import { useRef, useState } from "react";
import { OpenFolderButton } from "./components/OpenFolderButton";
import { PropertiesPanel } from "./components/PropertiesPanel";
import { SceneInspector } from "./components/SceneInspector";
import type { ViewportHandle } from "./components/Viewport";
import { Viewport } from "./components/Viewport";
import { saveScene } from "./services/scene-serializer";
import type { ResolvedScene } from "./types/scene";

function App() {
	const [scene, setScene] = useState<ResolvedScene | null>(null);
	const [dirHandle, setDirHandle] = useState<FileSystemDirectoryHandle | null>(
		null,
	);
	const [error, setError] = useState<string>("");
	const [selectedObjectKey, setSelectedObjectKey] = useState<string | null>(
		null,
	);
	const [sceneVersion, setSceneVersion] = useState(0);
	const [saving, setSaving] = useState(false);
	const viewportRef = useRef<ViewportHandle>(null);

	function handleSceneLoaded(s: ResolvedScene, dir: FileSystemDirectoryHandle) {
		setScene(s);
		setDirHandle(dir);
		setError("");
		setSelectedObjectKey(null);
		setSceneVersion((v) => v + 1);
	}

	/** Update scene data without triggering a full Three.js rebuild. */
	function handleSceneEdit(updater: (s: ResolvedScene) => ResolvedScene) {
		setScene((prev) => {
			if (!prev) return prev;
			return updater(prev);
		});
	}

	async function handleSave() {
		if (!scene || !dirHandle) return;
		setSaving(true);
		setError("");
		try {
			await saveScene(dirHandle, scene);
		} catch (err) {
			if (err instanceof DOMException && err.name === "AbortError") return;
			setError(err instanceof Error ? err.message : String(err));
		} finally {
			setSaving(false);
		}
	}

	const totalObjects = scene
		? [...scene.contexts, ...scene.layers].reduce(
				(s, l) => s + l.data.objects.length,
				0,
			)
		: 0;

	return (
		<div className="app-root">
			{/* Full-screen viewport */}
			<div className="viewport-fill">
				<Viewport
					ref={viewportRef}
					scene={scene}
					dirHandle={dirHandle}
					sceneVersion={sceneVersion}
					selectedObjectKey={selectedObjectKey}
					onSelectObject={setSelectedObjectKey}
				/>
			</div>

			{/* HUD overlay */}
			<div className="hud">
				{/* Top-left: header panel */}
				<div className="panel hud-header">
					<span className="wordmark">Skewer</span>
					<OpenFolderButton
						onSceneLoaded={handleSceneLoaded}
						onError={setError}
					/>
					<button
						type="button"
						className={`open-btn${saving ? " loading" : ""}`}
						disabled={!scene || !dirHandle || saving}
						onClick={handleSave}
					>
						{saving ? "Saving…" : "Save"}
					</button>
					{error && <span className="error-msg">{error}</span>}
				</div>

				{/* Left sidebar: scene inspector */}
				{scene && (
					<div className="panel hud-sidebar">
						<SceneInspector
							scene={scene}
							selectedObjectKey={selectedObjectKey}
							onSelectObject={setSelectedObjectKey}
						/>
					</div>
				)}

				{/* Right sidebar: properties panel */}
				{scene && selectedObjectKey && (
					<div className="panel hud-properties">
						<PropertiesPanel
							scene={scene}
							objectKey={selectedObjectKey}
							onSceneEdit={handleSceneEdit}
							viewportRef={viewportRef}
						/>
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
								<span className="stat-sep">&rarr;</span>
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
