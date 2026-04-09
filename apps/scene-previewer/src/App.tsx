import { useRef, useState } from "react";
import { LandingPage } from "./components/LandingPage";
import {
	MaterialPropertiesPanel,
	PropertiesPanel,
} from "./components/PropertiesPanel";
import { SceneInspector } from "./components/SceneInspector";
import type { ViewportHandle } from "./components/Viewport";
import { Viewport } from "./components/Viewport";
import { addRecentScene } from "./services/recent-scenes";
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
	const [selectedMaterialKey, setSelectedMaterialKey] = useState<string | null>(
		null,
	);

	function handleSelectObject(key: string | null) {
		setSelectedObjectKey(key);
		setSelectedMaterialKey(null);
	}

	function handleSelectMaterial(key: string | null) {
		setSelectedMaterialKey(key);
		setSelectedObjectKey(null);
	}
	const [sceneVersion, setSceneVersion] = useState(0);
	const [saving, setSaving] = useState(false);
	const [hasUnsavedChanges, setHasUnsavedChanges] = useState(false);
	const viewportRef = useRef<ViewportHandle>(null);

	function handleSceneLoaded(s: ResolvedScene, dir: FileSystemDirectoryHandle) {
		setScene(s);
		setDirHandle(dir);
		setError("");
		setSelectedObjectKey(null);
		setSelectedMaterialKey(null);
		setSceneVersion((v) => v + 1);
		setHasUnsavedChanges(false);
		addRecentScene(dir.name, dir);
	}

	/** Update scene data without triggering a full Three.js rebuild. */
	function handleSceneEdit(updater: (s: ResolvedScene) => ResolvedScene) {
		setScene((prev) => {
			if (!prev) return prev;
			setHasUnsavedChanges(true);
			return updater(prev);
		});
	}

	async function handleSave() {
		if (!scene || !dirHandle) return;
		setSaving(true);
		setError("");
		try {
			await saveScene(dirHandle, scene);
			setHasUnsavedChanges(false);
		} catch (err) {
			if (err instanceof DOMException && err.name === "AbortError") return;
			setError(err instanceof Error ? err.message : String(err));
		} finally {
			setSaving(false);
		}
	}

	function handleNavigateHome() {
		if (
			hasUnsavedChanges &&
			!window.confirm(
				"You have unsaved changes. Go back to the landing page and discard them?",
			)
		) {
			return;
		}
		setScene(null);
		setDirHandle(null);
		setSelectedObjectKey(null);
		setSelectedMaterialKey(null);
		setHasUnsavedChanges(false);
		setError("");
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
					onSelectObject={handleSelectObject}
				/>
			</div>

			{/* HUD overlay */}
			<div className="hud">
				{/* Top-left: header panel */}
				<div className="panel hud-header">
					<button
						type="button"
						className={`wordmark${scene ? " wordmark-link" : ""}`}
						onClick={handleNavigateHome}
						disabled={!scene}
					>
						Skewer
					</button>
					{scene && hasUnsavedChanges && (
						<button
							type="button"
							className={`open-btn${saving ? " loading" : ""}`}
							disabled={saving}
							onClick={handleSave}
						>
							{saving ? "Saving…" : "Save"}
						</button>
					)}
					{error && <span className="error-msg">{error}</span>}
				</div>

				{/* Left sidebar: scene inspector */}
				{scene && (
					<div className="panel hud-sidebar">
						<SceneInspector
							scene={scene}
							selectedObjectKey={selectedObjectKey}
							selectedMaterialKey={selectedMaterialKey}
							onSelectObject={handleSelectObject}
							onSelectMaterial={handleSelectMaterial}
						/>
					</div>
				)}

				{/* Right sidebar: properties panel */}
				{scene && (selectedObjectKey || selectedMaterialKey) && (
					<div className="panel hud-properties">
						{selectedObjectKey && (
							<PropertiesPanel
								scene={scene}
								objectKey={selectedObjectKey}
								onSceneEdit={handleSceneEdit}
								onRebuild={() => setSceneVersion((v) => v + 1)}
								viewportRef={viewportRef}
							/>
						)}
						{selectedMaterialKey && (
							<MaterialPropertiesPanel
								scene={scene}
								matKey={selectedMaterialKey}
								onSceneEdit={handleSceneEdit}
								viewportRef={viewportRef}
							/>
						)}
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

				{/* Landing page */}
				{!scene && (
					<LandingPage onSceneLoaded={handleSceneLoaded} onError={setError} />
				)}
			</div>
		</div>
	);
}

export default App;
