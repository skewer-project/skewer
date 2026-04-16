import { Camera, Maximize, Move, Rotate3d } from "lucide-react";
import { useCallback, useEffect, useRef, useState } from "react";
import { LandingPage } from "./components/LandingPage";
import {
	MaterialPropertiesPanel,
	PropertiesPanel,
} from "./components/PropertiesPanel";
import { SceneInspector } from "./components/SceneInspector";
import type { ViewportHandle } from "./components/Viewport";
import { Viewport } from "./components/Viewport";
import {
	countGraphNodes,
	deleteNodeAtPath,
	insertChild,
	updateNodeAtPath,
} from "./services/graph-path";
import { addRecentScene } from "./services/recent-scenes";
import { saveScene } from "./services/scene-serializer";
import type { Material, ResolvedScene, SceneNode } from "./types/scene";

function isEditableTarget(target: EventTarget | null) {
	if (!(target instanceof HTMLElement)) return false;
	if (target.isContentEditable) return true;
	return (
		target.tagName === "INPUT" ||
		target.tagName === "TEXTAREA" ||
		target.tagName === "SELECT"
	);
}

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
	const [transformMode, setTransformMode] = useState<
		"translate" | "rotate" | "scale"
	>("translate");
	const [transformSpace, setTransformSpace] = useState<"world" | "local">(
		"world",
	);

	const handleSelectObject = useCallback((key: string | null) => {
		setSelectedObjectKey(key);
		setSelectedMaterialKey(null);
	}, []);

	const handleSelectMaterial = useCallback((key: string | null) => {
		setSelectedMaterialKey(key);
		setSelectedObjectKey(null);
	}, []);
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
	const handleSceneEdit = useCallback(
		(updater: (s: ResolvedScene) => ResolvedScene) => {
			setScene((prev) => {
				if (!prev) return prev;
				setHasUnsavedChanges(true);
				return updater(prev);
			});
		},
		[],
	);

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

	const handleDeleteObject = useCallback(
		(objectKey: string) => {
			handleSceneEdit((s) => deleteNodeAtPath(s, objectKey));
			setSceneVersion((v) => v + 1);
			setSelectedObjectKey(null);
		},
		[handleSceneEdit],
	);

	const handleTransformChange = useCallback(
		(objectKey: string, transform: import("./types/scene").StaticTransform) => {
			if (!scene) return;
			handleSceneEdit((s) =>
				updateNodeAtPath(s, objectKey, (o) => ({ ...o, transform })),
			);
			setHasUnsavedChanges(true);
			viewportRef.current?.applyPatch(scene, objectKey, {
				kind: "node-transform",
				value: transform,
			});
		},
		[handleSceneEdit, scene],
	);

	useEffect(() => {
		if (!scene) return;
		const handleKeyDown = (event: KeyboardEvent) => {
			if (event.key !== "Delete" && event.key !== "Backspace") return;
			if (!selectedObjectKey) return;
			if (isEditableTarget(event.target)) return;
			event.preventDefault();
			handleDeleteObject(selectedObjectKey);
		};
		window.addEventListener("keydown", handleKeyDown);
		return () => window.removeEventListener("keydown", handleKeyDown);
	}, [scene, selectedObjectKey, handleDeleteObject]);

	const handleAddGraphNode = useCallback(
		(
			_tag: "ctx" | "lyr",
			_layerIdx: number,
			parentKey: string,
			node: SceneNode,
		) => {
			let childKey = "";
			handleSceneEdit((s) => {
				const r = insertChild(s, parentKey, node);
				childKey = r.childKey;
				return r.scene;
			});
			setSceneVersion((v) => v + 1);
			if (childKey) {
				setSelectedObjectKey(childKey);
				setSelectedMaterialKey(null);
			}
		},
		[handleSceneEdit],
	);

	const handleAddMaterial = useCallback(
		(tag: "ctx" | "lyr", layerIdx: number, name: string, mat: Material) => {
			handleSceneEdit((s) => {
				const listKey = tag === "ctx" ? "contexts" : "layers";
				const newList = [...s[listKey]];
				const newLayer = {
					...newList[layerIdx],
					data: {
						...newList[layerIdx].data,
						materials: { ...newList[layerIdx].data.materials, [name]: mat },
					},
				};
				newList[layerIdx] = newLayer;
				return { ...s, [listKey]: newList };
			});
			setSelectedMaterialKey(`${tag}:${layerIdx}:mat:${name}`);
			setSelectedObjectKey(null);
		},
		[handleSceneEdit],
	);

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
				(acc, l) => acc + countGraphNodes(l.data.graph),
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
					transformMode={transformMode}
					transformSpace={transformSpace}
					onTransformChange={handleTransformChange}
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

				{/* Transform mode toolbar */}
				{scene && selectedObjectKey && (
					<div className="panel hud-toolbar">
						<div className="toolbar-group">
							<button
								type="button"
								className={`toolbar-btn ${transformMode === "translate" ? "active" : ""}`}
								title="Move (G)"
								onClick={() => setTransformMode("translate")}
							>
								<Move size={16} />
							</button>
							<button
								type="button"
								className={`toolbar-btn ${transformMode === "rotate" ? "active" : ""}`}
								title="Rotate (R)"
								onClick={() => setTransformMode("rotate")}
							>
								<Rotate3d size={16} />
							</button>
							<button
								type="button"
								className={`toolbar-btn ${transformMode === "scale" ? "active" : ""}`}
								title="Scale (S)"
								onClick={() => setTransformMode("scale")}
							>
								<Maximize size={16} />
							</button>
						</div>
						<div className="toolbar-sep" />
						<div className="toolbar-group">
							<button
								type="button"
								className={`toolbar-btn ${transformSpace === "world" ? "active" : ""}`}
								title="World (.)"
								onClick={() => setTransformSpace("world")}
							>
								Global
							</button>
							<button
								type="button"
								className={`toolbar-btn ${transformSpace === "local" ? "active" : ""}`}
								title="Local (,)"
								onClick={() => setTransformSpace("local")}
							>
								Local
							</button>
						</div>
					</div>
				)}

				{/* Left sidebar: scene inspector */}
				{scene && dirHandle && (
					<div className="panel hud-sidebar">
						<SceneInspector
							scene={scene}
							selectedObjectKey={selectedObjectKey}
							selectedMaterialKey={selectedMaterialKey}
							onSelectObject={handleSelectObject}
							onSelectMaterial={handleSelectMaterial}
							onAddGraphNode={handleAddGraphNode}
							onAddMaterial={handleAddMaterial}
							dirHandle={dirHandle}
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
								onDeleteObject={() => handleDeleteObject(selectedObjectKey)}
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

				{/* Bottom-right: reset camera + stats */}
				{scene && (
					<div className="hud-bottom-stack">
						<button
							type="button"
							className="hud-reset-cam-btn"
							title="Reset view to scene camera"
							aria-label="Reset view to scene camera"
							onClick={() => viewportRef.current?.resetCameraToScene()}
						>
							<Camera size={16} strokeWidth={1.75} aria-hidden />
						</button>
						<div className="panel hud-stats">
							<span className="stat-tag stat-ctx">
								{scene.contexts.length}c
							</span>
							<span className="stat-sep">/</span>
							<span className="stat-tag stat-lyr">{scene.layers.length}L</span>
							<span className="stat-sep">/</span>
							<span className="stat-num">{totalObjects} nodes</span>
							{scene.output_dir && (
								<>
									<span className="stat-sep">&rarr;</span>
									<span className="stat-dir">{scene.output_dir}</span>
								</>
							)}
						</div>
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
