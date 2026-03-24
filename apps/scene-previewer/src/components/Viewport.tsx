import { useEffect, useRef } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { buildSceneGraph } from "../services/scene-to-three";
import type { ResolvedScene } from "../types/scene";

interface Props {
	scene?: ResolvedScene | null;
	dirHandle?: FileSystemDirectoryHandle | null;
}

export function Viewport({ scene, dirHandle }: Props) {
	const containerRef = useRef<HTMLDivElement>(null);

	// Refs shared between the two effects
	const threeScene = useRef<THREE.Scene | null>(null);
	const camera = useRef<THREE.PerspectiveCamera | null>(null);
	const controls = useRef<OrbitControls | null>(null);
	const sceneGroup = useRef<THREE.Group | null>(null);

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

		// XZ grid
		sc.add(new THREE.GridHelper(10, 10, 0x1e2229, 0x141619));

		// Lights (needed for Lambert / Standard / Physical materials)
		sc.add(new THREE.AmbientLight(0xffffff, 0.4));
		const sun = new THREE.DirectionalLight(0xffffff, 1.2);
		sun.position.set(5, 10, 7);
		sc.add(sun);

		// Scene-object group (placeholder, swapped out per scene load)
		const grp = new THREE.Group();
		sc.add(grp);
		sceneGroup.current = grp;

		// Resize
		const ro = new ResizeObserver(() => {
			cam.aspect = container.clientWidth / container.clientHeight;
			cam.updateProjectionMatrix();
			renderer.setSize(container.clientWidth, container.clientHeight);
		});
		ro.observe(container);

		// Render loop
		let animId: number;
		function animate() {
			animId = requestAnimationFrame(animate);
			ctrl.update();
			renderer.render(sc, cam);
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
		};
	}, []);

	// --- Effect 2: rebuild scene objects when scene/dirHandle change ---
	useEffect(() => {
		if (!scene || !dirHandle) return;

		const sc = threeScene.current;
		const cam = camera.current;
		const ctrl = controls.current;
		if (!sc || !cam || !ctrl) return;

		// Sync camera to scene's camera definition
		cam.fov = scene.camera.vfov;
		cam.position.set(...scene.camera.look_from);
		ctrl.target.set(...scene.camera.look_at);
		cam.updateProjectionMatrix();
		ctrl.update();

		// Abort any in-flight build from a previous scene load
		const abortController = new AbortController();

		buildSceneGraph(scene, dirHandle, abortController.signal).then((newGroup) => {
			if (abortController.signal.aborted) return;

			// Swap out the old group
			const old = sceneGroup.current;
			if (old) sc.remove(old);

			sc.add(newGroup);
			sceneGroup.current = newGroup;
		});

		return () => {
			abortController.abort();
		};
	}, [scene, dirHandle]);

	return <div ref={containerRef} style={{ width: "100%", height: "100%" }} />;
}
