import {
	type Ref,
	useCallback,
	useEffect,
	useImperativeHandle,
	useRef,
} from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { TransformControls } from "three/examples/jsm/controls/TransformControls.js";
import { EffectComposer } from "three/examples/jsm/postprocessing/EffectComposer.js";
import { OutlinePass } from "three/examples/jsm/postprocessing/OutlinePass.js";
import { OutputPass } from "three/examples/jsm/postprocessing/OutputPass.js";
import { RenderPass } from "three/examples/jsm/postprocessing/RenderPass.js";
import {
	collectAnimatedNodes,
	collectLeafKeysForMaterial,
	resolveNodeAtPath,
} from "../services/graph-path";
import {
	applyStaticTransformToObject3D,
	buildSceneGraph,
	makeThreeMaterial,
	revokeBlobUrls,
} from "../services/scene-to-three";
import {
	cameraHasKeyframes,
	evaluateCameraAt,
	evaluateTransformAt,
} from "../services/transform";
import type {
	CameraHandle,
	Material,
	ResolvedScene,
	StaticTransform,
	Vec3,
} from "../types/scene";
import { isAnimated } from "../types/scene";

const CAMERA_FROM_COLOR = 0x35d6ff;
const CAMERA_AT_COLOR = 0xf0b34d;
const CAMERA_SELECTED_COLOR = 0xffffff;

/** Get the world-space center of an object — bbox center if it has geometry, otherwise origin. */
function getWorldCenter(obj: THREE.Object3D, out: THREE.Vector3): void {
	const box = new THREE.Box3().setFromObject(obj);
	if (!box.isEmpty()) {
		box.getCenter(out);
	} else {
		obj.getWorldPosition(out);
	}
}

/** Align perspective camera and orbit target with the scene file camera. */
function syncOrbitCameraToScene(
	cam: THREE.PerspectiveCamera,
	ctrl: OrbitControls,
	sceneData: ResolvedScene,
	time = 0,
) {
	const c = evaluateCameraAt(sceneData.camera, time);
	cam.fov = c.vfov;
	cam.position.set(...c.look_from);
	ctrl.target.set(...c.look_at);
	cam.up.set(...c.vup);
	cam.updateProjectionMatrix();
	ctrl.update();
}

function makeCameraHandle(
	name: CameraHandle,
	color: number,
	radius: number,
): THREE.Mesh {
	const mesh = new THREE.Mesh(
		new THREE.SphereGeometry(radius, 18, 12),
		new THREE.MeshBasicMaterial({
			color,
			depthTest: false,
			transparent: true,
			opacity: 0.95,
		}),
	);
	mesh.name = `Camera ${name}`;
	mesh.renderOrder = 4;
	mesh.userData.cameraHandle = name;
	return mesh;
}

function makeCameraLine(
	name: string,
	color: number,
	opacity: number,
): THREE.LineSegments {
	const line = new THREE.LineSegments(
		new THREE.BufferGeometry(),
		new THREE.LineBasicMaterial({
			color,
			depthTest: false,
			transparent: true,
			opacity,
		}),
	);
	line.name = name;
	line.renderOrder = 3;
	return line;
}

function createCameraRig(): THREE.Group {
	const rig = new THREE.Group();
	rig.name = "SceneCameraRig";
	rig.userData.isCameraRig = true;

	rig.add(makeCameraHandle("look_from", CAMERA_FROM_COLOR, 0.12));
	rig.add(makeCameraHandle("look_at", CAMERA_AT_COLOR, 0.1));
	rig.add(makeCameraLine("CameraAimLine", 0xe8f6ff, 0.78));
	rig.add(makeCameraLine("CameraFrustum", CAMERA_FROM_COLOR, 0.62));
	return rig;
}

function disposeCameraRig(rig: THREE.Group) {
	rig.traverse((obj) => {
		if (obj instanceof THREE.Mesh || obj instanceof THREE.LineSegments) {
			obj.geometry?.dispose();
			disposeMaterial(obj.material);
		}
	});
}

function lineByName(rig: THREE.Group, name: string): THREE.LineSegments | null {
	const obj = rig.getObjectByName(name);
	return obj instanceof THREE.LineSegments ? obj : null;
}

function handleByName(rig: THREE.Group, name: CameraHandle): THREE.Mesh | null {
	const obj = rig.children.find(
		(child) => child.userData.cameraHandle === name,
	);
	return obj instanceof THREE.Mesh ? obj : null;
}

function setLinePoints(line: THREE.LineSegments, points: THREE.Vector3[]) {
	line.geometry.setFromPoints(points);
	line.geometry.computeBoundingSphere();
}

function updateCameraRig(
	rig: THREE.Group,
	sceneData: ResolvedScene,
	time: number,
	aspect: number,
	selectedHandle: CameraHandle | null,
) {
	const cam = evaluateCameraAt(sceneData.camera, time);
	const lookFrom = new THREE.Vector3(...cam.look_from);
	const lookAt = new THREE.Vector3(...cam.look_at);
	const upHint = new THREE.Vector3(...cam.vup);
	const forward = lookAt.clone().sub(lookFrom);
	const planeDistance = Math.max(forward.length(), 0.001);
	forward.normalize();
	if (forward.lengthSq() < 1e-12) forward.set(0, 0, -1);

	let right = new THREE.Vector3().crossVectors(upHint, forward).normalize();
	if (right.lengthSq() < 1e-12) right = new THREE.Vector3(1, 0, 0);
	const up = new THREE.Vector3().crossVectors(forward, right).normalize();

	const halfHeight =
		Math.tan(THREE.MathUtils.degToRad(cam.vfov) * 0.5) * planeDistance;
	const halfWidth = halfHeight * aspect;
	const center = lookFrom
		.clone()
		.add(forward.clone().multiplyScalar(planeDistance));
	const topLeft = center
		.clone()
		.add(up.clone().multiplyScalar(halfHeight))
		.sub(right.clone().multiplyScalar(halfWidth));
	const topRight = center
		.clone()
		.add(up.clone().multiplyScalar(halfHeight))
		.add(right.clone().multiplyScalar(halfWidth));
	const bottomRight = center
		.clone()
		.sub(up.clone().multiplyScalar(halfHeight))
		.add(right.clone().multiplyScalar(halfWidth));
	const bottomLeft = center
		.clone()
		.sub(up.clone().multiplyScalar(halfHeight))
		.sub(right.clone().multiplyScalar(halfWidth));
	const cameraFrame = new THREE.Matrix4().makeBasis(right, up, forward);

	const fromHandle = handleByName(rig, "look_from");
	const atHandle = handleByName(rig, "look_at");
	if (fromHandle) {
		fromHandle.position.copy(lookFrom);
		fromHandle.scale.setScalar(selectedHandle === "look_from" ? 1.35 : 1);
		(fromHandle.material as THREE.MeshBasicMaterial).color.setHex(
			selectedHandle === "look_from"
				? CAMERA_SELECTED_COLOR
				: CAMERA_FROM_COLOR,
		);
	}
	if (atHandle) {
		atHandle.position.copy(lookAt);
		atHandle.quaternion.setFromRotationMatrix(cameraFrame);
		atHandle.scale.setScalar(selectedHandle === "look_at" ? 1.35 : 1);
		(atHandle.material as THREE.MeshBasicMaterial).color.setHex(
			selectedHandle === "look_at" ? CAMERA_SELECTED_COLOR : CAMERA_AT_COLOR,
		);
	}

	const aimLine = lineByName(rig, "CameraAimLine");
	if (aimLine) setLinePoints(aimLine, [lookFrom, lookAt]);

	const frustum = lineByName(rig, "CameraFrustum");
	if (frustum) {
		setLinePoints(frustum, [
			lookFrom,
			topLeft,
			lookFrom,
			topRight,
			lookFrom,
			bottomRight,
			lookFrom,
			bottomLeft,
			topLeft,
			topRight,
			topRight,
			bottomRight,
			bottomRight,
			bottomLeft,
			bottomLeft,
			topLeft,
		]);
	}

	rig.visible = true;
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
	| { kind: "assign-material"; matData: Material; isVolumetric?: boolean }
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
	/** Global animation time (seconds); drives animated node transforms. */
	currentTime?: number;
	/** When true, the transform gizmo is disabled (no edit / commit while the timeline plays). */
	isPlaying?: boolean;
	selectedObjectKey: string | null;
	onSelectObject: (key: string | null) => void;
	selectedCameraHandle?: CameraHandle | null;
	onSelectCameraHandle?: (handle: CameraHandle | null) => void;
	cameraHandleEditingEnabled?: boolean;
	/** Transform gizmo mode: "translate" | "rotate" | "scale" */
	transformMode?: "translate" | "rotate" | "scale";
	/** Coordinate space: "world" | "local" | "object" */
	transformSpace?: "world" | "local";
	/** Called when gizmo transform changes */
	onTransformChange?: (objectKey: string, transform: StaticTransform) => void;
	onCameraHandleChange?: (handle: CameraHandle, value: Vec3) => void;
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

function findCameraHandle(obj: THREE.Object3D): CameraHandle | null {
	let cur: THREE.Object3D | null = obj;
	while (cur) {
		const handle = cur.userData.cameraHandle;
		if (handle === "look_from" || handle === "look_at") return handle;
		cur = cur.parent;
	}
	return null;
}

/** Collect all meshes under sceneGroup that match the given objectKey. */
function collectMeshesForKey(
	sceneGroup: THREE.Object3D,
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

/** First Group under root whose root carries this objectKey (node transform target). */
function findGroupByKey(root: THREE.Object3D, key: string): THREE.Group | null {
	let found: THREE.Group | null = null;
	root.traverse((obj) => {
		if (found) return;
		if (obj instanceof THREE.Group && obj.userData.objectKey === key) {
			found = obj;
		}
	});
	return found;
}

function applyAnimatedNodesAtTime(
	root: THREE.Group,
	resolvedScene: ResolvedScene,
	time: number,
	excludeKey?: string | null,
) {
	for (const { objectKey, transform } of collectAnimatedNodes(resolvedScene)) {
		if (objectKey === excludeKey) continue;
		const nodeGrp = findGroupByKey(root, objectKey);
		if (nodeGrp) {
			applyStaticTransformToObject3D(
				nodeGrp,
				evaluateTransformAt(transform, time),
			);
		}
	}
}

export function Viewport({
	scene,
	dirHandle,
	sceneVersion,
	currentTime = 0,
	isPlaying = false,
	selectedObjectKey,
	onSelectObject,
	selectedCameraHandle = null,
	onSelectCameraHandle,
	cameraHandleEditingEnabled = true,
	transformMode = "translate",
	transformSpace = "world",
	onTransformChange,
	onCameraHandleChange,
	ref,
}: Props & { ref?: Ref<ViewportHandle> }) {
	const containerRef = useRef<HTMLDivElement>(null);

	const threeScene = useRef<THREE.Scene | null>(null);
	const camera = useRef<THREE.PerspectiveCamera | null>(null);
	const controls = useRef<OrbitControls | null>(null);
	const sceneGroup = useRef<THREE.Group | null>(null);
	const blobUrlsRef = useRef<string[]>([]);
	const composer = useRef<EffectComposer | null>(null);
	const outlinePass = useRef<OutlinePass | null>(null);
	const transformControls = useRef<TransformControls | null>(null);
	const gizmoProxy = useRef<THREE.Group | null>(null);
	const cameraRigGroup = useRef<THREE.Group | null>(null);
	const gizmoAnchorTmp = useRef(new THREE.Vector3());
	const isDraggingRef = useRef(false);
	/** Drives gizmo re-attach after scene graph rebuild; reset when the Three tree is replaced. */
	const latestTransformRef = useRef<{
		objectKey: string | null;
		mode: "translate" | "rotate" | "scale";
		space: "world" | "local";
	}>({ objectKey: null, mode: "translate", space: "world" });
	const isPlayingRef = useRef(isPlaying);
	useEffect(() => {
		isPlayingRef.current = isPlaying;
	}, [isPlaying]);
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

	const currentTimeRef = useRef(currentTime);
	useEffect(() => {
		currentTimeRef.current = currentTime;
	}, [currentTime]);

	useEffect(() => {
		const rig = cameraRigGroup.current;
		const currentScene = scene;
		const cam = camera.current;
		if (!rig) return;
		if (!currentScene || !cam) {
			rig.visible = false;
			return;
		}
		updateCameraRig(
			rig,
			currentScene,
			currentTime,
			cam.aspect,
			selectedCameraHandle,
		);
	}, [scene, currentTime, selectedCameraHandle]);

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
				syncOrbitCameraToScene(cam, ctrl, currentScene, currentTimeRef.current);
			},
			applyPatch(scene: ResolvedScene, objectKey: string, patch: ThreePatch) {
				const sc = threeScene.current;
				if (!sc) return;

				if (patch.kind === "sphere-center") {
					const meshes = collectMeshesForKey(sc, objectKey);
					for (const m of meshes) m.position.set(...patch.value);
				} else if (patch.kind === "sphere-radius") {
					const meshes = collectMeshesForKey(sc, objectKey);
					for (const m of meshes) {
						if (m instanceof THREE.Mesh) {
							m.geometry.dispose();
							m.geometry = new THREE.SphereGeometry(patch.value, 32, 16);
						}
					}
				} else if (patch.kind === "quad-vertices") {
					const meshes = collectMeshesForKey(sc, objectKey);
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
					const nodeGrp = findGroupByKey(sc, objectKey);
					if (nodeGrp) {
						const proxy = gizmoProxy.current;
						const isSelected = proxy?.userData.target === nodeGrp;

						applyStaticTransformToObject3D(nodeGrp, patch.value);

						if (isSelected && proxy) {
							// If we are currently dragging, DON'T update the proxy position to avoid jitter.
							// If we are NOT dragging (e.g. typing into UI), update proxy to match new object transform.
							if (!proxy.userData.isDragging) {
								const center = gizmoAnchorTmp.current;
								const worldQuat = new THREE.Quaternion();

								getWorldCenter(nodeGrp, center);
								if (latestTransformRef.current.space === "local") {
									nodeGrp.getWorldQuaternion(worldQuat);
								} else {
									worldQuat.identity();
								}

								proxy.position.copy(center);
								proxy.quaternion.copy(worldQuat);
								proxy.scale.setScalar(1);
								proxy.updateMatrixWorld(true);
							}
						}
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
						const res = resolveNodeAtPath(currentScene, key);
						const isVol =
							res?.node.kind === "sphere" && !!res.node.inside_medium;
						const meshes = collectMeshesForKey(sc, key);
						for (const m of meshes) {
							if (m instanceof THREE.Mesh) {
								if (isVol) {
									// For volumes, we don't update the base material because it's a fixed ghost visualization
									continue;
								}
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
					const meshes = collectMeshesForKey(sc, objectKey);
					const newMat = makeThreeMaterial(patch.matData);
					for (const m of meshes) {
						if (m instanceof THREE.Mesh) {
							if (patch.isVolumetric) {
								// Switch to volumetric visualization if it wasn't one, or keep if it is
								if (
									!(m.material instanceof THREE.MeshPhongMaterial) ||
									m.material.opacity !== 0.3
								) {
									disposeMaterial(m.material);
									m.material = new THREE.MeshPhongMaterial({
										color: 0x4488ff,
										transparent: true,
										opacity: 0.3,
										depthWrite: false,
										side: THREE.BackSide,
									});
									// Add wireframe if missing
									if (m.children.length === 0) {
										const wire = new THREE.Mesh(
											m.geometry,
											new THREE.MeshBasicMaterial({
												color: 0x4488ff,
												wireframe: true,
												transparent: true,
												opacity: 0.2,
											}),
										);
										m.add(wire);
									}
								}
								continue;
							}

							// If switching AWAY from volumetric, remove wireframe children
							if (m.children.length > 0) {
								for (const child of [...m.children]) {
									if (child instanceof THREE.Mesh) {
										child.geometry.dispose();
										disposeMaterial(child.material);
										m.remove(child);
									}
								}
							}

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

		const proxy = new THREE.Group();
		proxy.name = "GizmoProxy";
		sc.add(proxy);
		gizmoProxy.current = proxy;

		const cameraRig = createCameraRig();
		cameraRig.visible = false;
		sc.add(cameraRig);
		cameraRigGroup.current = cameraRig;

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

		// TransformControls (gizmo)
		const tctrl = new TransformControls(cam, renderer.domElement);
		tctrl.setSize(0.75);
		tctrl.setSpace("world");
		tctrl.setMode("translate");
		sc.add(tctrl.getHelper());
		transformControls.current = tctrl;

		const handleDraggingChanged = (event: { value: unknown }) => {
			ctrl.enabled = !event.value;
			if (gizmoProxy.current) {
				gizmoProxy.current.userData.isDragging = event.value;
			}
		};
		tctrl.addEventListener("dragging-changed", handleDraggingChanged);

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
			const proxy = gizmoProxy.current;
			if (proxy?.userData.target) {
				proxy.userData.target = null;
			}
			const cameraRig = cameraRigGroup.current;
			if (cameraRig) {
				sc.remove(cameraRig);
				disposeCameraRig(cameraRig);
			}
			ctrl.dispose();
			tctrl.removeEventListener("dragging-changed", handleDraggingChanged);
			tctrl.dispose();

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
			transformControls.current = null;
			gizmoProxy.current = null;
			cameraRigGroup.current = null;
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
			syncOrbitCameraToScene(cam, ctrl, currentScene, currentTimeRef.current);
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

				const proxy = gizmoProxy.current;
				const tctrl = transformControls.current;
				if (proxy?.userData.target) {
					proxy.userData.target = null;
				}
				tctrl?.detach();
				latestTransformRef.current.objectKey = null;

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
				const builtScene = latestSceneRef.current;
				if (builtScene) {
					applyAnimatedNodesAtTime(
						result.group,
						builtScene,
						currentTimeRef.current,
					);
				}
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

	// --- Effect 4: position proxy gizmo on selected object (NO reparenting) ---
	useEffect(() => {
		const tctrl = transformControls.current;
		const grp = sceneGroup.current;
		if (!tctrl || !grp) return;

		const mode = transformMode ?? "translate";
		const space = transformSpace ?? "world";

		tctrl.setMode(mode);
		// TransformControls only understands "world" | "local"
		// "object" is our custom mode meaning bbox-center + local axes
		tctrl.setSpace(space === "world" ? "world" : "local");
		latestTransformRef.current.mode = mode;
		latestTransformRef.current.space = space === "world" ? "world" : "local";

		if (
			selectedObjectKey &&
			(selectedObjectKey !== latestTransformRef.current.objectKey ||
				mode !== latestTransformRef.current.mode)
		) {
			const nodeGrp = findGroupByKey(grp, selectedObjectKey);
			const proxy = gizmoProxy.current;
			if (nodeGrp && proxy) {
				if (proxy.userData.target !== nodeGrp) proxy.userData.target = null;

				delete proxy.userData.startProxyMatrix;
				delete proxy.userData.startTargetMatrix;

				proxy.userData.target = nodeGrp;
				proxy.userData.isDragging = false;

				tctrl.attach(proxy);
				latestTransformRef.current.objectKey = selectedObjectKey;
			}
		} else if (!selectedObjectKey) {
			const proxy = gizmoProxy.current;
			if (proxy) {
				proxy.userData.target = null;
				proxy.userData.isDragging = false;
			}
			tctrl.detach();
			latestTransformRef.current.objectKey = null;
			return;
		}

		const nodeGrp = findGroupByKey(grp, selectedObjectKey);
		const proxy = gizmoProxy.current;
		if (!nodeGrp || !proxy) return;

		nodeGrp.updateMatrixWorld(true);

		const worldPos = new THREE.Vector3();
		const worldQuat = new THREE.Quaternion();

		// Gizmo always sits at the object's visual center (bbox), regardless of space.
		// Space only controls axis orientation.
		getWorldCenter(nodeGrp, worldPos);

		if (space === "local") {
			nodeGrp.getWorldQuaternion(worldQuat);
		} else {
			// "world": axes align to world, identity quaternion
			worldQuat.identity();
		}

		proxy.position.copy(worldPos);
		proxy.quaternion.copy(worldQuat);
		proxy.scale.setScalar(1);
		proxy.userData.target = nodeGrp;
		proxy.userData.isDragging = false;
		proxy.updateMatrixWorld(true);

		tctrl.attach(proxy);
		latestTransformRef.current.objectKey = selectedObjectKey;
	}, [selectedObjectKey, transformMode, transformSpace]);

	// --- Disable transform gizmo while timeline is playing ---
	useEffect(() => {
		const tctrl = transformControls.current;
		if (tctrl) tctrl.enabled = !isPlaying;
	}, [isPlaying]);

	// --- Effect 5: apply gizmo delta to target (NO reparenting ever) ---
	useEffect(() => {
		const tctrl = transformControls.current;
		const grp = sceneGroup.current;
		if (!tctrl || !onTransformChange || !selectedObjectKey || !grp) return;

		const onDraggingChanged = (event: THREE.Event & { value: unknown }) => {
			const proxy = gizmoProxy.current;
			if (!proxy?.userData.target) return;

			if (event.value) {
				const nodeGrp = proxy.userData.target as THREE.Group;
				nodeGrp.updateMatrixWorld(true);
				proxy.updateMatrixWorld(true);
				proxy.userData.proxyMatrixAtDragStart = proxy.matrixWorld.clone();
				proxy.userData.targetMatrixAtDragStart = nodeGrp.matrixWorld.clone();
				proxy.userData.isDragging = true;
				isDraggingRef.current = true;
				delete proxy.userData.pendingTransform;
			} else {
				proxy.userData.isDragging = false;
				isDraggingRef.current = false;
				const pending = proxy.userData.pendingTransform as
					| StaticTransform
					| undefined;
				if (pending) {
					onTransformChange(selectedObjectKey, pending);
					delete proxy.userData.pendingTransform;
				}
			}
		};

		const onChangeDuringDrag = () => {
			if (isPlayingRef.current) return;
			const proxy = gizmoProxy.current;
			if (!proxy?.userData.isDragging || !proxy.userData.target) return;

			const nodeGrp = proxy.userData.target as THREE.Group;
			const startProxy = proxy.userData.proxyMatrixAtDragStart as THREE.Matrix4;
			const startTarget = proxy.userData
				.targetMatrixAtDragStart as THREE.Matrix4;
			if (!startProxy || !startTarget) return;

			const startProxyInv = startProxy.clone().invert();
			proxy.updateMatrixWorld(true);
			const deltaWorld = proxy.matrixWorld.clone().multiply(startProxyInv);
			const newTargetWorld = deltaWorld.multiply(startTarget);

			const parent = nodeGrp.parent;
			if (parent) {
				parent.updateMatrixWorld(true);
				const parentInv = parent.matrixWorld.clone().invert();
				const newLocal = parentInv.multiply(newTargetWorld);
				nodeGrp.matrix.copy(newLocal);
				nodeGrp.matrix.decompose(
					nodeGrp.position,
					nodeGrp.quaternion,
					nodeGrp.scale,
				);
			} else {
				newTargetWorld.decompose(
					nodeGrp.position,
					nodeGrp.quaternion,
					nodeGrp.scale,
				);
			}
			nodeGrp.updateMatrixWorld(true);
			const scl = nodeGrp.scale;

			const transform: StaticTransform = {
				translate: [nodeGrp.position.x, nodeGrp.position.y, nodeGrp.position.z],
				rotate: [
					THREE.MathUtils.radToDeg(nodeGrp.rotation.x),
					THREE.MathUtils.radToDeg(nodeGrp.rotation.y),
					THREE.MathUtils.radToDeg(nodeGrp.rotation.z),
				],
				scale: (() => {
					const isUniform =
						Math.abs(scl.x - scl.y) < 1e-4 && Math.abs(scl.y - scl.z) < 1e-4;
					if (transformMode === "scale") {
						return isUniform ? scl.x : [scl.x, scl.y, scl.z];
					}
					if (
						Math.abs(scl.x - 1) < 1e-4 &&
						Math.abs(scl.y - 1) < 1e-4 &&
						Math.abs(scl.z - 1) < 1e-4
					) {
						return undefined;
					}
					return isUniform ? scl.x : [scl.x, scl.y, scl.z];
				})(),
			};

			// Store for commit on drag end; also live-commit for static nodes
			proxy.userData.pendingTransform = transform;
			const s = latestSceneRef.current;
			const hit = s ? resolveNodeAtPath(s, selectedObjectKey) : null;
			const isAnim =
				hit?.node.transform !== undefined && isAnimated(hit.node.transform);
			if (!isAnim) {
				onTransformChange(selectedObjectKey, transform);
			}
		};

		tctrl.addEventListener("change", onChangeDuringDrag);
		tctrl.addEventListener("dragging-changed", onDraggingChanged);

		return () => {
			tctrl.removeEventListener("change", onChangeDuringDrag);
			tctrl.removeEventListener("dragging-changed", onDraggingChanged);
		};
	}, [selectedObjectKey, transformMode, onTransformChange]);

	// --- Effect: attach translate gizmo to selected camera handle ---
	useEffect(() => {
		const tctrl = transformControls.current;
		const rig = cameraRigGroup.current;
		if (!tctrl || !rig) return;

		if (
			!selectedCameraHandle ||
			selectedObjectKey ||
			!cameraHandleEditingEnabled
		) {
			if (!selectedObjectKey) tctrl.detach();
			return;
		}

		const handle = handleByName(rig, selectedCameraHandle);
		if (!handle) return;

		tctrl.setMode("translate");
		tctrl.setSpace(selectedCameraHandle === "look_at" ? "local" : "world");
		tctrl.attach(handle);

		const onDraggingChanged = (event: THREE.Event & { value: unknown }) => {
			handle.userData.isDragging = event.value;
			isDraggingRef.current = Boolean(event.value);
		};

		const onChangeDuringDrag = () => {
			if (isPlayingRef.current || !handle.userData.isDragging) return;
			handle.updateMatrixWorld(true);
			const p = handle.position;
			onCameraHandleChange?.(selectedCameraHandle, [p.x, p.y, p.z]);
		};

		tctrl.addEventListener("change", onChangeDuringDrag);
		tctrl.addEventListener("dragging-changed", onDraggingChanged);

		return () => {
			tctrl.removeEventListener("change", onChangeDuringDrag);
			tctrl.removeEventListener("dragging-changed", onDraggingChanged);
			handle.userData.isDragging = false;
		};
	}, [
		selectedCameraHandle,
		selectedObjectKey,
		cameraHandleEditingEnabled,
		onCameraHandleChange,
	]);

	// --- Effect: apply animated transforms; keep proxy snapped to target ---
	useEffect(() => {
		const grp = sceneGroup.current;
		const currentScene = scene;
		if (!grp || !currentScene) return;

		const cam = camera.current;
		const ctrl = controls.current;
		if (cam && ctrl && cameraHasKeyframes(currentScene.camera)) {
			syncOrbitCameraToScene(cam, ctrl, currentScene, currentTime);
		}

		const proxy = gizmoProxy.current;
		const isDragging = isDraggingRef.current;
		const excludeKey = isDragging ? selectedObjectKey : null;

		applyAnimatedNodesAtTime(grp, currentScene, currentTime, excludeKey);

		// Snap proxy to follow the animated target (only when not dragging)
		if (!selectedObjectKey || !proxy?.userData.target || isDragging) return;

		const nodeGrp = proxy.userData.target as THREE.Group;
		nodeGrp.updateMatrixWorld(true);

		const worldPos = new THREE.Vector3();
		const worldQuat = new THREE.Quaternion();

		// Always snap to visual center
		getWorldCenter(nodeGrp, worldPos);

		if (transformSpace === "local") {
			nodeGrp.getWorldQuaternion(worldQuat);
		} else {
			worldQuat.identity();
		}

		proxy.position.copy(worldPos);
		proxy.quaternion.copy(worldQuat);
		proxy.scale.setScalar(1);
		proxy.updateMatrixWorld(true);
	}, [scene, currentTime, selectedObjectKey, transformSpace]);

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

			const rig = cameraRigGroup.current;
			if (rig) {
				const cameraHits = raycaster.intersectObjects(rig.children, true);
				const handleHit = cameraHits.find((hit) =>
					findCameraHandle(hit.object),
				);
				if (handleHit) {
					const handle = findCameraHandle(handleHit.object);
					if (handle) {
						onSelectCameraHandle?.(
							handle === selectedCameraHandle ? null : handle,
						);
						return;
					}
				}
			}

			const targets: THREE.Object3D[] = [grp];
			if (gizmoProxy.current) targets.push(gizmoProxy.current);
			const hits = raycaster.intersectObjects(targets, true);

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
			onSelectCameraHandle?.(null);
		},
		[
			selectedCameraHandle,
			selectedObjectKey,
			onSelectCameraHandle,
			onSelectObject,
		],
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
}
