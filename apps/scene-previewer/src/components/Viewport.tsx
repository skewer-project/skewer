import { useCallback, useEffect, useRef } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { EffectComposer } from "three/examples/jsm/postprocessing/EffectComposer.js";
import { OutlinePass } from "three/examples/jsm/postprocessing/OutlinePass.js";
import { OutputPass } from "three/examples/jsm/postprocessing/OutputPass.js";
import { RenderPass } from "three/examples/jsm/postprocessing/RenderPass.js";
import { buildSceneGraph } from "../services/scene-to-three";
import type { ResolvedScene } from "../types/scene";

interface Props {
	scene?: ResolvedScene | null;
	dirHandle?: FileSystemDirectoryHandle | null;
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

export function Viewport({
	scene,
	dirHandle,
	selectedObjectKey,
	onSelectObject,
}: Props) {
	const containerRef = useRef<HTMLDivElement>(null);

	const threeScene = useRef<THREE.Scene | null>(null);
	const camera = useRef<THREE.PerspectiveCamera | null>(null);
	const controls = useRef<OrbitControls | null>(null);
	const sceneGroup = useRef<THREE.Group | null>(null);
	const composer = useRef<EffectComposer | null>(null);
	const outlinePass = useRef<OutlinePass | null>(null);

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
			renderer.dispose();
			container.removeChild(renderer.domElement);
			threeScene.current = null;
			camera.current = null;
			controls.current = null;
			sceneGroup.current = null;
			composer.current = null;
			outlinePass.current = null;
		};
	}, []);

	// --- Effect 2: rebuild scene objects when scene/dirHandle change ---
	useEffect(() => {
		if (!scene || !dirHandle) return;

		const sc = threeScene.current;
		const cam = camera.current;
		const ctrl = controls.current;
		if (!sc || !cam || !ctrl) return;

		// Sync camera
		cam.fov = scene.camera.vfov;
		cam.position.set(...scene.camera.look_from);
		ctrl.target.set(...scene.camera.look_at);
		cam.updateProjectionMatrix();
		ctrl.update();

		const abortController = new AbortController();

		buildSceneGraph(scene, dirHandle, abortController.signal).then(
			(newGroup) => {
				if (abortController.signal.aborted) return;

				const old = sceneGroup.current;
				if (old) sc.remove(old);

				sc.add(newGroup);
				sceneGroup.current = newGroup;
			},
		);

		return () => {
			abortController.abort();
		};
	}, [scene, dirHandle]);

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
			style={{ width: "100%", height: "100%" }}
			onMouseDown={handleMouseDown}
			onMouseUp={handleMouseUp}
		/>
	);
}
