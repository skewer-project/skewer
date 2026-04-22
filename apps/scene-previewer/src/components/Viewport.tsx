import {
	forwardRef,
	useCallback,
	useEffect,
	useImperativeHandle,
	useRef,
} from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { EffectComposer } from "three/examples/jsm/postprocessing/EffectComposer.js";
import { OutlinePass } from "three/examples/jsm/postprocessing/OutlinePass.js";
import { OutputPass } from "three/examples/jsm/postprocessing/OutputPass.js";
import { RenderPass } from "three/examples/jsm/postprocessing/RenderPass.js";
import { collectLeafKeysForMaterial } from "../services/graph-path";
import {
	applyStaticTransformToObject3D,
	buildSceneGraph,
	makeThreeMaterial,
	revokeBlobUrls,
} from "../services/scene-to-three";
import type {
	Material,
	ResolvedScene,
	StaticTransform,
	Vec3,
} from "../types/scene";

/** Align perspective camera and orbit target with the scene file camera. */
function syncOrbitCameraToScene(
	cam: THREE.PerspectiveCamera,
	ctrl: OrbitControls,
	sceneData: ResolvedScene,
) {
	const c = sceneData.camera;
	cam.fov = c.vfov;
	cam.position.set(...c.look_from);
	ctrl.target.set(...c.look_at);
	cam.up.set(...c.vup);
	cam.updateProjectionMatrix();
	ctrl.update();
}

// ── Patch types for incremental Three.js updates ────────────

export type ThreePatch =
	| { kind: "sphere-center"; value: Vec3 }
	| { kind: "sphere-radius"; value: number }
	| { kind: "quad-vertices"; value: [Vec3, Vec3, Vec3, Vec3] }
	| { kind: "node-transform"; value: StaticTransform }
	| {
			kind: "material";
			matData: Material;
			matName: string;
			layerTag: string;
			layerIdx: number;
	  }
	| { kind: "assign-material"; matData: Material }
	| { kind: "rebuild" };

export interface ViewportHandle {
	applyPatch(scene: ResolvedScene, objectKey: string, patch: ThreePatch): void;
	/** Restore orbit camera position, target, and up vector from the loaded scene JSON. */
	resetCameraToScene(): void;
}

// ── Helpers ─────────────────────────────────────────────────

function disposeMaterialTextures(material: THREE.Material) {
	for (const value of Object.values(material)) {
		if (value instanceof THREE.Texture) {
			value.dispose();
		} else if (Array.isArray(value)) {
			for (const item of value) {
				if (item instanceof THREE.Texture) item.dispose();
			}
		}
	}
}

function disposeMaterial(material: THREE.Material | THREE.Material[]) {
	if (Array.isArray(material)) {
		for (const mat of material) disposeMaterial(mat);
		return;
	}
	disposeMaterialTextures(material);
	material.dispose();
}

function getMaterialSide(
	material: THREE.Material | THREE.Material[] | undefined,
): THREE.Side | null {
	if (!material) return null;
	if (Array.isArray(material)) {
		if (material.some((m) => m.side === THREE.DoubleSide)) {
			return THREE.DoubleSide;
		}
		return material[0]?.side ?? null;
	}
	return material.side;
}

function disposeSceneGroup(group: THREE.Group) {
	group.traverse((obj) => {
		if (obj instanceof THREE.Mesh) {
			obj.geometry?.dispose();
			disposeMaterial(obj.material);
		}
	});
}

interface Props {
	scene?: ResolvedScene | null;
	dirHandle?: FileSystemDirectoryHandle | null;
	// Incremented when the scene structure changes (add/remove/reorder objects).
	sceneVersion: number;
	selectedObjectKey: string | null;
	onSelectObject: (key: string | null) => void;
}

/** Walk up the Three.js parent chain to find the first object with an objectKey. */
function findObjectKey(obj: THREE.Object3D): string | null {
	let cur: THREE.Object3D | null = obj;
	while (cur) {
		if (cur.userData.objectKey) return cur.userData.objectKey as string;
		cur = cur.parent;
	}
	return null;
}

/** Collect all meshes under sceneGroup that match the given objectKey. */
function collectMeshesForKey(
	sceneGroup: THREE.Group,
	key: string,
): THREE.Object3D[] {
	const meshes: THREE.Object3D[] = [];
	sceneGroup.traverse((child) => {
		if (child instanceof THREE.Mesh && child.userData.objectKey === key) {
			meshes.push(child);
		}
	});
	return meshes;
}

/** First Group in the scene whose root carries this objectKey (node transform target). */
function findGroupByKey(
	sceneGroup: THREE.Group,
	key: string,
): THREE.Group | null {
	let found: THREE.Group | null = null;
	sceneGroup.traverse((obj) => {
		if (found) return;
		if (obj instanceof THREE.Group && obj.userData.objectKey === key) {
			found = obj;
		}
	});
	return found;
}

export const Viewport = forwardRef<ViewportHandle, Props>(function Viewport(
	{ scene, dirHandle, sceneVersion, selectedObjectKey, onSelectObject },
	ref,
) {
	const containerRef = useRef<HTMLDivElement>(null);

	const threeScene = useRef<THREE.Scene | null>(null);
	const camera = useRef<THREE.PerspectiveCamera | null>(null);
	const controls = useRef<OrbitControls | null>(null);
	const sceneGroup = useRef<THREE.Group | null>(null);
	const blobUrlsRef = useRef<string[]>([]);
	const composer = useRef<EffectComposer | null>(null);
	const outlinePass = useRef<OutlinePass | null>(null);
	// Used to avoid rebuilding the whole graph on minor edits.
	const lastBuild = useRef<{
		dirHandle: FileSystemDirectoryHandle | null;
		sceneVersion: number | null;
	}>({ dirHandle: null, sceneVersion: null });

	// Track previous dirHandle to distinguish a new scene load from a sceneVersion bump
	const prevDirHandle = useRef<FileSystemDirectoryHandle | null | undefined>(
		undefined,
	);
	const latestSceneRef = useRef(scene);
	useEffect(() => {
		latestSceneRef.current = scene;
	}, [scene]);

	// ── Imperative handle: applyPatch ──
	// Live, incremental updates for edit controls; full rebuilds happen elsewhere.
	useImperativeHandle(
		ref,
		() => ({
			resetCameraToScene() {
				const currentScene = latestSceneRef.current;
				const cam = camera.current;
				const ctrl = controls.current;
				if (!currentScene || !cam || !ctrl) return;
				syncOrbitCameraToScene(cam, ctrl, currentScene);
			},
			applyPatch(scene: ResolvedScene, objectKey: string, patch: ThreePatch) {
				const grp = sceneGroup.current;
				if (!grp) return;

				if (patch.kind === "sphere-center") {
					const meshes = collectMeshesForKey(grp, objectKey);
					for (const m of meshes) m.position.set(...patch.value);
				} else if (patch.kind === "sphere-radius") {
					const meshes = collectMeshesForKey(grp, objectKey);
					for (const m of meshes) {
						if (m instanceof THREE.Mesh) {
							m.geometry.dispose();
							m.geometry = new THREE.SphereGeometry(patch.value, 32, 16);
						}
					}
				} else if (patch.kind === "quad-vertices") {
					const meshes = collectMeshesForKey(grp, objectKey);
					const [p0, p1, p2, p3] = patch.value;
					for (const m of meshes) {
						if (m instanceof THREE.Mesh) {
							const pos = m.geometry.getAttribute("position");
							if (pos) {
								const arr = pos.array as Float32Array;
								// Triangle 1: p0, p1, p2
								arr.set(p0, 0);
								arr.set(p1, 3);
								arr.set(p2, 6);
								// Triangle 2: p0, p2, p3
								arr.set(p0, 9);
								arr.set(p2, 12);
								arr.set(p3, 15);
								pos.needsUpdate = true;
								m.geometry.computeVertexNormals();
								m.geometry.computeBoundingSphere();
							}
						}
					}
				} else if (patch.kind === "node-transform") {
					const nodeGrp = findGroupByKey(grp, objectKey);
					if (nodeGrp) {
						applyStaticTransformToObject3D(nodeGrp, patch.value);
					}
				} else if (patch.kind === "material") {
					// Update ALL objects in the layer that use this material name
					const currentScene = scene;
					if (!currentScene) return;
					const list =
						patch.layerTag === "ctx"
							? currentScene.contexts
							: currentScene.layers;
					const layer = list[patch.layerIdx];
					if (!layer) return;

					// Find all object keys in this layer that reference the edited material
					const tag = patch.layerTag;
					const li = patch.layerIdx;
					const keysToUpdate = collectLeafKeysForMaterial(
						layer.data,
						tag as "ctx" | "lyr",
						li,
						patch.matName,
					);

					const newMat = makeThreeMaterial(patch.matData);
					for (const key of keysToUpdate) {
						const meshes = collectMeshesForKey(grp, key);
						for (const m of meshes) {
							if (m instanceof THREE.Mesh) {
								const oldSide = getMaterialSide(m.material);
								disposeMaterial(m.material);
								m.material = newMat.clone();
								if (oldSide === THREE.DoubleSide) {
									(m.material as THREE.Material).side = THREE.DoubleSide;
								}
							}
						}
					}
				} else if (patch.kind === "assign-material") {
					// Reassign material for a single object
					const meshes = collectMeshesForKey(grp, objectKey);
					const newMat = makeThreeMaterial(patch.matData);
					for (const m of meshes) {
						if (m instanceof THREE.Mesh) {
							const oldSide = getMaterialSide(m.material);
							disposeMaterial(m.material);
							m.material = newMat.clone();
							if (oldSide === THREE.DoubleSide) {
								(m.material as THREE.Material).side = THREE.DoubleSide;
							}
						}
					}
				}
				// kind === "rebuild" is handled by incrementing sceneVersion externally
			},
		}),
		[],
	);

	// --- Effect 1: one-time renderer / camera / controls setup ---
	useEffect(() => {
		const container = containerRef.current;
		if (!container) return;

		const renderer = new THREE.WebGLRenderer({ antialias: true });
		renderer.setPixelRatio(window.devicePixelRatio);
		renderer.setSize(container.clientWidth, container.clientHeight);
		container.appendChild(renderer.domElement);

		const sc = new THREE.Scene();
		sc.background = new THREE.Color(0x0c0d0f);
		threeScene.current = sc;

		const cam = new THREE.PerspectiveCamera(
			50,
			container.clientWidth / container.clientHeight,
			0.01,
			1000,
		);
		cam.position.set(5, 4, 7);
		camera.current = cam;

		const ctrl = new OrbitControls(cam, renderer.domElement);
		ctrl.enableDamping = true;
		ctrl.dampingFactor = 0.08;
		controls.current = ctrl;

		// Axes (X=red, Y=green, Z=blue)
		const axes = new THREE.AxesHelper(2);
		(axes.material as THREE.LineBasicMaterial).depthTest = false;
		axes.renderOrder = 1;
		sc.add(axes);

		// Lights
		sc.add(new THREE.AmbientLight(0xffffff, 0.4));
		const sun = new THREE.DirectionalLight(0xffffff, 1.2);
		sun.position.set(5, 10, 7);
		sc.add(sun);

		// Scene-object group
		const grp = new THREE.Group();
		sc.add(grp);
		sceneGroup.current = grp;

		// Post-processing with OutlinePass
		const comp = new EffectComposer(renderer);
		comp.addPass(new RenderPass(sc, cam));

		const outline = new OutlinePass(
			new THREE.Vector2(container.clientWidth, container.clientHeight),
			sc,
			cam,
		);
		outline.edgeStrength = 3;
		outline.edgeGlow = 0.3;
		outline.edgeThickness = 1.2;
		outline.visibleEdgeColor.set(0xe8a53c); // amber
		outline.hiddenEdgeColor.set(0xe8a53c);
		outline.pulsePeriod = 0;
		comp.addPass(outline);
		comp.addPass(new OutputPass());

		composer.current = comp;
		outlinePass.current = outline;

		// Resize
		const ro = new ResizeObserver(() => {
			const w = container.clientWidth;
			const h = container.clientHeight;
			cam.aspect = w / h;
			cam.updateProjectionMatrix();
			renderer.setSize(w, h);
			comp.setSize(w, h);
		});
		ro.observe(container);

		// Render loop
		let animId: number;
		function animate() {
			animId = requestAnimationFrame(animate);
			ctrl.update();
			comp.render();
		}
		animate();

		return () => {
			cancelAnimationFrame(animId);
			ro.disconnect();
			ctrl.dispose();

			const old = sceneGroup.current;
			if (old) {
				sc.remove(old);
				disposeSceneGroup(old);
			}
			const oldUrls = blobUrlsRef.current;
			if (oldUrls.length > 0) {
				revokeBlobUrls(oldUrls);
			}
			const comp = composer.current;
			if (comp) {
				for (const pass of comp.passes) {
					if ("dispose" in pass && typeof pass.dispose === "function") {
						pass.dispose();
					}
				}
				const compAny = comp as unknown as {
					renderTarget1?: THREE.WebGLRenderTarget;
					renderTarget2?: THREE.WebGLRenderTarget;
				};
				compAny.renderTarget1?.dispose();
				compAny.renderTarget2?.dispose();
				if ("dispose" in comp && typeof comp.dispose === "function") {
					comp.dispose();
				}
			}

			renderer.dispose();
			container.removeChild(renderer.domElement);
			threeScene.current = null;
			camera.current = null;
			controls.current = null;
			sceneGroup.current = null;
			blobUrlsRef.current = [];
			composer.current = null;
			outlinePass.current = null;
		};
	}, []);

	// --- Effect 2: rebuild scene objects on structural changes ---
	useEffect(() => {
		const currentScene = scene;
		if (!currentScene || !dirHandle) return;

		const sc = threeScene.current;
		const cam = camera.current;
		const ctrl = controls.current;
		if (!sc || !cam || !ctrl) return;

		// Only reset the camera when a new scene is loaded (dirHandle changed),
		// not on sceneVersion bumps — otherwise edits reset the viewport angle.
		const isNewScene = dirHandle !== prevDirHandle.current;
		prevDirHandle.current = dirHandle;
		if (isNewScene) {
			syncOrbitCameraToScene(cam, ctrl, currentScene);
		}

		if (
			lastBuild.current.dirHandle === dirHandle &&
			lastBuild.current.sceneVersion === sceneVersion
		) {
			return;
		}
		lastBuild.current = { dirHandle, sceneVersion };

		const abortController = new AbortController();

		buildSceneGraph(currentScene, dirHandle, abortController.signal).then(
			(result) => {
				if (abortController.signal.aborted) return;

				const old = sceneGroup.current;
				if (old) {
					sc.remove(old);
					disposeSceneGroup(old);
				}

				const oldUrls = blobUrlsRef.current;
				if (oldUrls.length > 0) {
					revokeBlobUrls(oldUrls);
				}

				sc.add(result.group);
				sceneGroup.current = result.group;
				blobUrlsRef.current = result.blobUrls;
			},
		);

		return () => {
			abortController.abort();
		};
	}, [dirHandle, scene, sceneVersion]);

	// --- Effect 3: update outline when selection changes ---
	useEffect(() => {
		const outline = outlinePass.current;
		const grp = sceneGroup.current;
		if (!outline) return;

		if (!selectedObjectKey || !grp) {
			outline.selectedObjects = [];
			return;
		}

		outline.selectedObjects = collectMeshesForKey(grp, selectedObjectKey);
	}, [selectedObjectKey]);

	// --- Click handler for raycasting ---
	const handleClick = useCallback(
		(e: React.MouseEvent<HTMLDivElement>) => {
			const container = containerRef.current;
			const cam = camera.current;
			const grp = sceneGroup.current;
			if (!container || !cam || !grp) return;

			// Only handle plain left-clicks (no drag)
			if (e.button !== 0) return;

			const rect = container.getBoundingClientRect();
			const mouse = new THREE.Vector2(
				((e.clientX - rect.left) / rect.width) * 2 - 1,
				-((e.clientY - rect.top) / rect.height) * 2 + 1,
			);

			const raycaster = new THREE.Raycaster();
			raycaster.setFromCamera(mouse, cam);
			const hits = raycaster.intersectObjects(grp.children, true);

			if (hits.length > 0) {
				const key = findObjectKey(hits[0].object);
				if (key) {
					// Toggle off if clicking same object
					onSelectObject(key === selectedObjectKey ? null : key);
					return;
				}
			}
			// Click on empty space — deselect
			onSelectObject(null);
		},
		[selectedObjectKey, onSelectObject],
	);

	// Track mouse-down position to distinguish clicks from drags
	const mouseDown = useRef<{ x: number; y: number } | null>(null);

	const handleMouseDown = useCallback((e: React.MouseEvent) => {
		mouseDown.current = { x: e.clientX, y: e.clientY };
	}, []);

	const handleMouseUp = useCallback(
		(e: React.MouseEvent<HTMLDivElement>) => {
			if (!mouseDown.current) return;
			const dx = e.clientX - mouseDown.current.x;
			const dy = e.clientY - mouseDown.current.y;
			mouseDown.current = null;
			// Only treat as click if mouse barely moved (not a drag/orbit)
			if (dx * dx + dy * dy < 9) {
				handleClick(e);
			}
		},
		[handleClick],
	);

	return (
		<div
			ref={containerRef}
			role="application"
			aria-label="3D scene viewport"
			style={{ width: "100%", height: "100%" }}
			onMouseDown={handleMouseDown}
			onMouseUp={handleMouseUp}
		/>
	);
});
