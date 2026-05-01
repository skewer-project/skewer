import { Camera, Cloud, Maximize, Move, Rotate3d, X } from "lucide-react";
import {
	useCallback,
	useEffect,
	useMemo,
	useRef,
	useState,
	useSyncExternalStore,
} from "react";
import { JobsModal } from "./components/JobsModal";
import { LandingPage } from "./components/LandingPage";
import {
	MaterialPropertiesPanel,
	MediumPropertiesPanel,
	PropertiesPanel,
} from "./components/PropertiesPanel";
import { RenderConfirmDialog } from "./components/RenderConfirmDialog";
import { SceneInspector } from "./components/SceneInspector";
import { Timeline } from "./components/Timeline";
import { UserMenu } from "./components/UserMenu";
import type { ViewportHandle } from "./components/Viewport";
import { Viewport } from "./components/Viewport";
import type { CloudJobRenderConfig } from "./services/cloud-job-types";
import { resumePendingJobs, startCloudRender } from "./services/cloud-render";
import {
	countGraphNodes,
	deleteNodeAtPath,
	insertChild,
	resolveNodeAtPath,
	updateMedium,
	updateNodeAtPath,
} from "./services/graph-path";
import {
	getSnapshot as getJobsSnapshot,
	isNonTerminalStatus,
	subscribe as subscribeJobs,
} from "./services/jobs-store";
import { addRecentScene } from "./services/recent-scenes";
import { saveScene } from "./services/scene-serializer";
import {
	applyStaticTransformToAnimatedAtTime,
	collectSceneKeyframeTimes,
	evaluateTransformAt,
	getAnimationRange,
} from "./services/transform";
import type {
	Material,
	Medium,
	RenderConfig,
	ResolvedScene,
	SceneNode,
	Vec3,
} from "./types/scene";
import { isAnimated } from "./types/scene";

function isEditableTarget(target: EventTarget | null) {
	if (!(target instanceof HTMLElement)) return false;
	if (target.isContentEditable) return true;
	return (
		target.tagName === "INPUT" ||
		target.tagName === "TEXTAREA" ||
		target.tagName === "SELECT"
	);
}

function JobsCloudButton({ onClick }: { onClick: () => void }) {
	const jobs = useSyncExternalStore(
		subscribeJobs,
		getJobsSnapshot,
		getJobsSnapshot,
	);
	const running = jobs.filter((j) => isNonTerminalStatus(j.status)).length;
	const hasFailed = jobs.some((j) => j.status === "failed");
	const state = running > 0 ? "active" : hasFailed ? "error" : "idle";
	const label =
		running > 0
			? `${running} render${running > 1 ? "s" : ""} in progress`
			: hasFailed
				? "Cloud renders, last attempt failed"
				: "Cloud renders";

	return (
		<button
			type="button"
			className={`jobs-cloud-btn jobs-cloud-${state}`}
			aria-label={label}
			title={label}
			onClick={onClick}
		>
			<Cloud size={16} aria-hidden />
			{running > 0 ? (
				<span className="jobs-cloud-badge" aria-hidden>
					{running}
				</span>
			) : hasFailed ? (
				<span className="jobs-cloud-error-dot" aria-hidden />
			) : null}
		</button>
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
	const [selectedMediumKey, setSelectedMediumKey] = useState<string | null>(
		null,
	);
	const [transformMode, setTransformMode] = useState<
		"translate" | "rotate" | "scale"
	>("translate");
	const [transformSpace, setTransformSpace] = useState<"world" | "local">(
		"world",
	);

	const [renderSettings, setRenderSettings] = useState<RenderConfig>({
		integrator: "path_trace",
		max_samples: 128,
		min_samples: 16,
		max_depth: 8,
		threads: 0,
		noise_threshold: 0.01,
		enable_deep: false,
		image: {
			width: 1920,
			height: 1080,
		},
	});
	const renderStartTime = scene?.animation.start ?? 0;
	const renderEndTime = scene?.animation.end ?? 0;
	const renderFps = scene?.animation.fps ?? 24;

	const handleSelectObject = useCallback((key: string | null) => {
		setSelectedObjectKey(key);
		setSelectedMaterialKey(null);
		setSelectedMediumKey(null);
	}, []);

	const handleSelectMaterial = useCallback((key: string | null) => {
		setSelectedMaterialKey(key);
		setSelectedObjectKey(null);
		setSelectedMediumKey(null);
	}, []);

	const handleSelectMedium = useCallback((key: string | null) => {
		setSelectedMediumKey(key);
		setSelectedObjectKey(null);
		setSelectedMaterialKey(null);
	}, []);
	const [sceneVersion, setSceneVersion] = useState(0);
	const [saving, setSaving] = useState(false);
	const [showRenderDialog, setShowRenderDialog] = useState(false);
	const [showJobsModal, setShowJobsModal] = useState(false);
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
		setSelectedMediumKey(null);
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

	const updateAnimation = useCallback(
		(patch: Partial<import("./types/scene").Animation>) => {
			handleSceneEdit((s) => ({
				...s,
				animation: { ...s.animation, ...patch },
			}));
		},
		[handleSceneEdit],
	);
	const setRenderStartTime = useCallback(
		(n: number) => updateAnimation({ start: n }),
		[updateAnimation],
	);
	const setRenderEndTime = useCallback(
		(n: number) => updateAnimation({ end: n }),
		[updateAnimation],
	);
	const setRenderFps = useCallback(
		(n: number) => updateAnimation({ fps: n }),
		[updateAnimation],
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
			const ctx = resolveNodeAtPath(scene, objectKey);
			if (ctx) {
				const tr = ctx.node.transform;
				if (tr !== undefined && isAnimated(tr)) {
					const nextAnim = applyStaticTransformToAnimatedAtTime(
						tr,
						currentTime,
						transform,
					);
					const evaluated = evaluateTransformAt(nextAnim, currentTime);
					handleSceneEdit((s) => {
						let s2 = updateNodeAtPath(s, objectKey, (o) => ({
							...o,
							transform: nextAnim,
						}));
						const node = ctx.node;
						if (
							node.kind === "sphere" &&
							node.inside_medium &&
							evaluated.translate
						) {
							s2 = updateMedium(
								s2,
								`${ctx.tag}:${ctx.layerIdx}`,
								node.inside_medium,
								(m) => ({
									...m,
									translate: evaluated.translate as Vec3,
								}),
							);
						}
						return s2;
					});
					viewportRef.current?.applyPatch(scene, objectKey, {
						kind: "node-transform",
						value: evaluated,
					});
					return;
				}
			}
			handleSceneEdit((s) => {
				let s2 = updateNodeAtPath(s, objectKey, (o) => ({ ...o, transform }));
				if (ctx) {
					const node = ctx.node;
					if (
						node.kind === "sphere" &&
						node.inside_medium &&
						transform.translate
					) {
						s2 = updateMedium(
							s2,
							`${ctx.tag}:${ctx.layerIdx}`,
							node.inside_medium,
							(m) => ({
								...m,
								translate: transform.translate as Vec3,
							}),
						);
					}
				}
				return s2;
			});
			viewportRef.current?.applyPatch(scene, objectKey, {
				kind: "node-transform",
				value: transform,
			});
		},
		[handleSceneEdit, scene, currentTime],
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
				setSelectedMediumKey(null);
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
			setSelectedMediumKey(null);
		},
		[handleSceneEdit],
	);

	const handleAddMedium = useCallback(
		(tag: "ctx" | "lyr", layerIdx: number, name: string, medium: Medium) => {
			handleSceneEdit((s) => {
				const listKey = tag === "ctx" ? "contexts" : "layers";
				const newList = [...s[listKey]];
				const newLayer = {
					...newList[layerIdx],
					data: {
						...newList[layerIdx].data,
						media: { ...newList[layerIdx].data.media, [name]: medium },
					},
				};
				newList[layerIdx] = newLayer;
				return { ...s, [listKey]: newList };
			});
			setSelectedMediumKey(`${tag}:${layerIdx}:med:${name}`);
			setSelectedObjectKey(null);
			setSelectedMaterialKey(null);
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

	useEffect(() => {
		resumePendingJobs();
	}, []);

	useEffect(() => {
		if (!error) return;
		const t = window.setTimeout(() => setError(""), 6000);
		return () => window.clearTimeout(t);
	}, [error]);

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
					isPlaying={isPlaying}
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
					{scene && (
						<button
							type="button"
							className="open-btn open-btn-primary"
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
				</div>

				{/* Top-right: account + cloud jobs */}
				<div className="panel hud-account">
					<JobsCloudButton onClick={() => setShowJobsModal(true)} />
					<UserMenu onError={setError} />
				</div>

				{/* Error toast */}
				{error && (
					<div className="hud-toast hud-toast-error" role="alert">
						<span className="hud-toast-msg" title={error}>
							{error}
						</span>
						<button
							type="button"
							className="hud-toast-x"
							aria-label="Dismiss"
							onClick={() => setError("")}
						>
							<X size={12} />
						</button>
					</div>
				)}

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
								title="World"
								onClick={() => setTransformSpace("world")}
							>
								Global
							</button>
							<button
								type="button"
								className={`toolbar-btn ${transformSpace === "local" ? "active" : ""}`}
								title="Local"
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
							selectedMediumKey={selectedMediumKey}
							onSelectObject={handleSelectObject}
							onSelectMaterial={handleSelectMaterial}
							onSelectMedium={handleSelectMedium}
							onAddGraphNode={handleAddGraphNode}
							onAddMaterial={handleAddMaterial}
							onAddMedium={handleAddMedium}
							dirHandle={dirHandle}
							renderSettings={renderSettings}
							onRenderSettingsChange={setRenderSettings}
							startTime={renderStartTime}
							onStartTimeChange={setRenderStartTime}
							endTime={renderEndTime}
							onEndTimeChange={setRenderEndTime}
							fps={renderFps}
							onFpsChange={setRenderFps}
						/>
					</div>
				)}

				{/* Right sidebar: properties panel */}
				{scene &&
					(selectedObjectKey || selectedMaterialKey || selectedMediumKey) && (
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
									dirHandle={dirHandle as FileSystemDirectoryHandle}
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
							{selectedMediumKey && (
								<MediumPropertiesPanel
									scene={scene}
									medKey={selectedMediumKey}
									onSceneEdit={handleSceneEdit}
									viewportRef={viewportRef}
									dirHandle={dirHandle as FileSystemDirectoryHandle}
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

				{showJobsModal && (
					<JobsModal
						scene={scene}
						dirHandle={dirHandle}
						onClose={() => setShowJobsModal(false)}
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

				{/* Render Confirmation Dialog */}
				{scene && showRenderDialog && (
					<RenderConfirmDialog
						settings={renderSettings}
						startTime={renderStartTime}
						endTime={renderEndTime}
						fps={renderFps}
						onCancel={() => setShowRenderDialog(false)}
						onConfirm={async () => {
							setShowRenderDialog(false);
							if (!scene || !dirHandle) return;
							setError("");
							const startFrame = Math.round(renderStartTime * renderFps);
							const endFrame = Math.round(renderEndTime * renderFps);
							const renderConfig: CloudJobRenderConfig = {
								width: renderSettings.image.width,
								height: renderSettings.image.height,
								minSamples: renderSettings.min_samples,
								maxSamples: renderSettings.max_samples,
								maxDepth: renderSettings.max_depth,
								integrator: renderSettings.integrator,
								startTime: renderStartTime,
								endTime: renderEndTime,
								fps: renderFps,
								startFrame,
								endFrame,
								isAnimation: endFrame > startFrame,
							};
							try {
								await startCloudRender({
									scene,
									dir: dirHandle,
									enableCache: true,
									renderConfig,
								});
							} catch (e) {
								const msg = e instanceof Error ? e.message : String(e);
								setError(msg);
							}
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
