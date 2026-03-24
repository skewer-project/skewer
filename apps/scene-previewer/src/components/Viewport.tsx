import { useEffect, useRef } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";

export function Viewport() {
	const containerRef = useRef<HTMLDivElement>(null);

	useEffect(() => {
		const container = containerRef.current;
		if (!container) return;

		// --- Renderer ---
		const renderer = new THREE.WebGLRenderer({ antialias: true });
		renderer.setPixelRatio(window.devicePixelRatio);
		renderer.setSize(container.clientWidth, container.clientHeight);
		container.appendChild(renderer.domElement);

		// --- Scene ---
		const scene = new THREE.Scene();
		scene.background = new THREE.Color(0x1a1a1a);

		// --- Camera ---
		const camera = new THREE.PerspectiveCamera(
			50,
			container.clientWidth / container.clientHeight,
			0.01,
			1000,
		);
		camera.position.set(5, 4, 7);
		camera.lookAt(0, 0, 0);

		// --- Controls ---
		const controls = new OrbitControls(camera, renderer.domElement);
		controls.enableDamping = true;
		controls.dampingFactor = 0.08;

		// --- Axes helper (X=red, Y=green, Z=blue) ---
		const axes = new THREE.AxesHelper(2);
		// Disable depth test so axes always render on top of the grid (no z-fighting).
		(axes.material as THREE.LineBasicMaterial).depthTest = false;
		axes.renderOrder = 1;
		scene.add(axes);

		// --- Grid on XZ plane ---
		const grid = new THREE.GridHelper(10, 10, 0x444444, 0x2a2a2a);
		scene.add(grid);

		// --- Resize observer ---
		const resizeObserver = new ResizeObserver(() => {
			const w = container.clientWidth;
			const h = container.clientHeight;
			camera.aspect = w / h;
			camera.updateProjectionMatrix();
			renderer.setSize(w, h);
		});
		resizeObserver.observe(container);

		// --- Render loop ---
		let animId: number;
		function animate() {
			animId = requestAnimationFrame(animate);
			controls.update();
			renderer.render(scene, camera);
		}
		animate();

		return () => {
			cancelAnimationFrame(animId);
			resizeObserver.disconnect();
			controls.dispose();
			renderer.dispose();
			container.removeChild(renderer.domElement);
		};
	}, []);

	return <div ref={containerRef} style={{ width: "100%", height: "100%" }} />;
}
