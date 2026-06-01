import { createRef } from "react";
import * as THREE from "three";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, render } from "vitest-browser-react";
import { Viewport, type ViewportHandle } from "../../src/components/Viewport";
import type { ResolvedScene } from "../../src/types/scene";
import {
	asDirectoryHandle,
	MemoryDirectoryHandle,
} from "../services/test-utils";

const mocks = vi.hoisted(() => {
	return {
		buildSceneGraph: vi.fn(),
		buildSkyboxTexture: vi.fn(),
		revokeBlobUrls: vi.fn(),
	};
});

vi.mock("../../src/services/scene-to-three", async (importOriginal) => {
	const actual =
		await importOriginal<typeof import("../../src/services/scene-to-three")>();
	return {
		...actual,
		buildSceneGraph: mocks.buildSceneGraph,
		buildSkyboxTexture: mocks.buildSkyboxTexture,
		revokeBlobUrls: mocks.revokeBlobUrls,
	};
});

vi.mock("three", async (importOriginal) => {
	const actual = await importOriginal<typeof import("three")>();
	class FakeWebGLRenderer {
		readonly domElement = document.createElement("canvas");
		setPixelRatio() {}
		setSize() {}
		dispose() {}
	}
	return { ...actual, WebGLRenderer: FakeWebGLRenderer };
});

vi.mock("three/examples/jsm/controls/OrbitControls.js", () => {
	class OrbitControls extends THREE.EventDispatcher {
		target = new THREE.Vector3();
		enableDamping = false;
		dampingFactor = 0;
		enabled = true;
		update() {
			return false;
		}
		dispose() {}
	}
	return { OrbitControls };
});

vi.mock("three/examples/jsm/controls/TransformControls.js", () => {
	class TransformControls extends THREE.EventDispatcher {
		enabled = true;
		private helper = new THREE.Group();
		addEventListener = super.addEventListener;
		removeEventListener = super.removeEventListener;
		attach() {}
		detach() {}
		dispose() {}
		getHelper() {
			return this.helper;
		}
		setMode() {}
		setSize() {}
		setSpace() {}
	}
	return { TransformControls };
});

vi.mock("three/examples/jsm/postprocessing/EffectComposer.js", () => {
	class EffectComposer {
		passes: unknown[] = [];
		renderTarget1 = { dispose: vi.fn() };
		renderTarget2 = { dispose: vi.fn() };
		addPass(pass: unknown) {
			this.passes.push(pass);
		}
		setSize() {}
		render() {}
		dispose() {}
	}
	return { EffectComposer };
});

vi.mock("three/examples/jsm/postprocessing/OutlinePass.js", () => {
	class OutlinePass {
		selectedObjects: THREE.Object3D[] = [];
		visibleEdgeColor = { set: vi.fn() };
		hiddenEdgeColor = { set: vi.fn() };
		edgeStrength = 0;
		edgeGlow = 0;
		edgeThickness = 0;
		pulsePeriod = 0;
		dispose() {}
	}
	return { OutlinePass };
});

vi.mock("three/examples/jsm/postprocessing/RenderPass.js", () => {
	class RenderPass {
		dispose() {}
	}
	return { RenderPass };
});

vi.mock("three/examples/jsm/postprocessing/OutputPass.js", () => {
	class OutputPass {
		dispose() {}
	}
	return { OutputPass };
});

function makeScene(): ResolvedScene {
	return {
		camera: {
			look_from: [0, 0, 5],
			look_at: [0, 0, 0],
			vup: [0, 1, 0],
			vfov: 45,
			aperture_radius: 0,
			focus_distance: 5,
		},
		contexts: [],
		layers: [],
		output_dir: "renders",
		animation: { start: 0, end: 0, fps: 24, shutter_angle: 180 },
		settings: {
			integrator: "path_trace",
			max_samples: 64,
			min_samples: 4,
			max_depth: 6,
			threads: 0,
			image: { width: 640, height: 360 },
		},
	};
}

function makeBuiltGroup() {
	const group = new THREE.Group();
	const objectRoot = new THREE.Group();
	objectRoot.userData.objectKey = "lyr:0:0";
	const mesh = new THREE.Mesh(
		new THREE.SphereGeometry(1, 8, 8),
		new THREE.MeshBasicMaterial(),
	);
	objectRoot.add(mesh);
	group.add(objectRoot);
	return { group, mesh };
}

beforeEach(() => {
	class FakeResizeObserver {
		constructor(private readonly callback: ResizeObserverCallback) {}
		observe() {
			this.callback([], this as unknown as ResizeObserver);
		}
		disconnect() {}
		unobserve() {}
	}
	vi.stubGlobal("ResizeObserver", FakeResizeObserver);
	vi.stubGlobal("requestAnimationFrame", (cb: FrameRequestCallback) => {
		cb(0);
		return 1;
	});
	vi.stubGlobal("cancelAnimationFrame", vi.fn());
});

afterEach(async () => {
	await cleanup();
	vi.restoreAllMocks();
	mocks.buildSceneGraph.mockReset();
	mocks.buildSkyboxTexture.mockReset();
	mocks.revokeBlobUrls.mockReset();
});

describe("Viewport", () => {
	it("disposes replaced scene graphs and revokes stale blob URLs on rebuild", async () => {
		const first = makeBuiltGroup();
		const second = makeBuiltGroup();
		const firstSkybox = { dispose: vi.fn() } as unknown as THREE.CubeTexture;
		const secondSkybox = { dispose: vi.fn() } as unknown as THREE.CubeTexture;

		const firstGeometryDispose = vi.spyOn(first.mesh.geometry, "dispose");
		const firstMaterialDispose = vi.spyOn(first.mesh.material, "dispose");

		mocks.buildSceneGraph
			.mockResolvedValueOnce({
				group: first.group,
				blobUrls: ["blob:first"],
				skyboxTexture: firstSkybox,
			})
			.mockResolvedValueOnce({
				group: second.group,
				blobUrls: ["blob:second"],
				skyboxTexture: secondSkybox,
			});

		const ref = createRef<ViewportHandle>();
		const dirHandle = asDirectoryHandle(MemoryDirectoryHandle.fromFiles({}));
		const scene = makeScene();
		const screen = await render(
			<Viewport
				ref={ref}
				scene={scene}
				dirHandle={dirHandle}
				sceneVersion={1}
				selectedObjectKey={null}
				onSelectObject={vi.fn()}
			/>,
		);

		await vi.waitFor(() => {
			expect(mocks.buildSceneGraph).toHaveBeenCalledTimes(1);
		});

		await screen.rerender(
			<Viewport
				ref={ref}
				scene={scene}
				dirHandle={dirHandle}
				sceneVersion={2}
				selectedObjectKey={null}
				onSelectObject={vi.fn()}
			/>,
		);

		await vi.waitFor(() => {
			expect(mocks.buildSceneGraph).toHaveBeenCalledTimes(2);
		});

		expect(mocks.revokeBlobUrls).toHaveBeenCalledWith(["blob:first"]);
		expect(firstSkybox.dispose).toHaveBeenCalledOnce();
		expect(firstGeometryDispose).toHaveBeenCalled();
		expect(firstMaterialDispose).toHaveBeenCalled();
		expect(secondSkybox.dispose).not.toHaveBeenCalled();
	});
});
