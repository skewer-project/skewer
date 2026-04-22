import { Camera, Cloud } from "lucide-react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { LandingPage } from "./components/LandingPage";
import {
	MaterialPropertiesPanel,
	PropertiesPanel,
} from "./components/PropertiesPanel";
import { RenderSettingsDialog } from "./components/RenderSettingsDialog";
import { SceneInspector } from "./components/SceneInspector";
import { Timeline } from "./components/Timeline";
import type { ViewportHandle } from "./components/Viewport";
import { Viewport } from "./components/Viewport";
import {
	countGraphNodes,
	deleteNodeAtPath,
	insertChild,
} from "./services/graph-path";
import { addRecentScene } from "./services/recent-scenes";
import { saveScene } from "./services/scene-serializer";
import {
	collectSceneKeyframeTimes,
	getAnimationRange,
} from "./services/transform";
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
	const [showRenderDialog, setShowRenderDialog] = useState(false);
	const [hasUnsavedChanges, setHasUnsavedChanges] = useState(false);
	const [currentTime, setCurrentTime] = useState(0);
	const [isPlaying, setIsPlaying] = useState(false);
	const viewportRef = useRef<ViewportHandle>(null);
	const animRangeRef = useRef({ start: 0, end: 0 });

	function handleSceneLoaded(s: ResolvedScene, dir: FileSystemDirectoryHandle) {
		setScene(s);
		setDirHandle(dir);
		setError("");
		setSelectedObjectKey(null);
		setSelectedMaterialKey(null);
		setSceneVersion((v) => v + 1);
		setHasUnsavedChanges(false);
		setCurrentTime(0);
		setIsPlaying(false);
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

	const animRange = useMemo(
		() => (scene ? getAnimationRange(scene) : { start: 0, end: 0 }),
		[scene],
	);
	animRangeRef.current = animRange;

	const timelineKeyframeTimes = useMemo(
		() => (scene ? collectSceneKeyframeTimes(scene) : []),
		[scene],
	);

	useEffect(() => {
		if (!isPlaying || !scene) return;
		let id = 0;
		let last = performance.now();
		const tick = (now: number) => {
			const dt = (now - last) / 1000;
			last = now;
			setCurrentTime((t) => {
				const { start, end } = animRangeRef.current;
				const len = end - start;
				if (len <= 1e-6) return start;
				let next = t + dt;
				while (next > end) next -= len;
				return next;
			});
			id = requestAnimationFrame(tick);
		};
		id = requestAnimationFrame(tick);
		return () => cancelAnimationFrame(id);
	}, [isPlaying, scene]);

	useEffect(() => {
		if (!scene) return;
		const onKey = (event: KeyboardEvent) => {
			if (event.code !== "Space") return;
			if (isEditableTarget(event.target)) return;
			event.preventDefault();
			setIsPlaying((p) => !p);
		};
		window.addEventListener("keydown", onKey);
		return () => window.removeEventListener("keydown", onKey);
	}, [scene]);

	return (
		<div className="app-root">
			{/* Full-screen viewport */}
			<div className="viewport-fill">
				<Viewport
					ref={viewportRef}
					scene={scene}
					dirHandle={dirHandle}
					sceneVersion={sceneVersion}
					currentTime={currentTime}
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
					{scene && (
						<button
							type="button"
							className="open-btn"
							onClick={() => setShowRenderDialog(true)}
						>
							<Cloud
								size={14}
								style={{ verticalAlign: "middle", marginRight: "6px" }}
							/>
							Render
						</button>
					)}
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
								currentTime={currentTime}
								onTimeChange={setCurrentTime}
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
					<Timeline
						currentTime={currentTime}
						onTimeChange={setCurrentTime}
						isPlaying={isPlaying}
						onTogglePlay={() => setIsPlaying((p) => !p)}
						animRange={animRange}
						keyframeTimes={timelineKeyframeTimes}
					/>
				)}

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

				{/* Render Dialog */}
				{scene && showRenderDialog && (
					<RenderSettingsDialog
						scene={scene}
						onCancel={() => setShowRenderDialog(false)}
						onRender={(_config) => {
							setShowRenderDialog(false);
							// Backend integration will go here
						}}
					/>
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
