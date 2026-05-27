import { beforeEach, describe, expect, it, vi } from "vitest";
import { ApiError } from "../../src/services/api/types";
import type { BundleFile } from "../../src/services/bundle";
import type { CloudJob } from "../../src/services/cloud-job-types";
import { startCloudRender } from "../../src/services/cloud-render";
import type { ResolvedScene } from "../../src/types/scene";
import { asDirectoryHandle, MemoryDirectoryHandle } from "./test-utils";

const mocks = vi.hoisted(() => {
	let jobs: CloudJob[] = [];

	const patchJob = vi.fn((id: string, patch: Partial<CloudJob>) => {
		jobs = jobs.map((job) => {
			if (job.id !== id) return job;
			return { ...job, ...patch, id: patch.id ?? job.id };
		});
	});

	return {
		get jobs() {
			return jobs;
		},
		set jobs(next: CloudJob[]) {
			jobs = next;
		},
		collectSceneBundle: vi.fn(),
		cancelJob: vi.fn(),
		fetchCompositeBlob: vi.fn(),
		getCompositeArtifactURL: vi.fn(),
		getJobStatus: vi.fn(),
		initJob: vi.fn(),
		submitJob: vi.fn(),
		getCurrentUser: vi.fn(),
		signInWithGoogle: vi.fn(),
		upsertJob: vi.fn((job: CloudJob) => {
			jobs = [job, ...jobs.filter((existing) => existing.id !== job.id)];
		}),
		patchJob,
		removeJob: vi.fn((id: string) => {
			jobs = jobs.filter((job) => job.id !== id);
		}),
		addUploadedBytes: vi.fn((id: string, delta: number) => {
			patchJob(id, {
				uploadedBytes:
					(jobs.find((job) => job.id === id)?.uploadedBytes ?? 0) + delta,
			});
		}),
	};
});

vi.mock("../../src/services/bundle", () => ({
	collectSceneBundle: mocks.collectSceneBundle,
}));

vi.mock("../../src/services/api/jobs", () => ({
	cancelJob: mocks.cancelJob,
	fetchCompositeBlob: mocks.fetchCompositeBlob,
	getCompositeArtifactURL: mocks.getCompositeArtifactURL,
	getJobStatus: mocks.getJobStatus,
	initJob: mocks.initJob,
	submitJob: mocks.submitJob,
}));

vi.mock("../../src/services/auth", () => ({
	getCurrentUser: mocks.getCurrentUser,
	signInWithGoogle: mocks.signInWithGoogle,
}));

vi.mock("../../src/services/jobs-store", () => ({
	addUploadedBytes: mocks.addUploadedBytes,
	getSnapshot: () => mocks.jobs,
	isNonTerminalStatus: (status: CloudJob["status"]) =>
		!["succeeded", "failed", "cancelled"].includes(status),
	patchJob: mocks.patchJob,
	removeJob: mocks.removeJob,
	upsertJob: mocks.upsertJob,
	whenHydrated: () => Promise.resolve(),
}));

describe("cloud-render", () => {
	beforeEach(() => {
		mocks.jobs = [];
		vi.clearAllMocks();
		vi.stubGlobal(
			"fetch",
			vi.fn().mockResolvedValue({ ok: true, status: 200 }),
		);
		vi.stubGlobal("URL", {
			createObjectURL: vi.fn(() => "blob:preview"),
			revokeObjectURL: vi.fn(),
		});
		mocks.getCurrentUser.mockReturnValue({ uid: "user" });
		mocks.getCompositeArtifactURL.mockResolvedValue(
			"https://signed/stitched.mp4",
		);
		mocks.fetchCompositeBlob.mockResolvedValue(new Blob(["png"]));
		mocks.getJobStatus.mockResolvedValue({
			pipeline_id: "pipe-1",
			status: "PIPELINE_STATUS_SUCCEEDED",
			composite_output: "gs://bucket/render/custom.png",
		});
	});

	it("packages, uploads, submits, and hydrates previews", async () => {
		const scene = makeScene();
		const files: BundleFile[] = [
			{
				path: "scene.json",
				blob: new Blob(["scene"]),
				contentType: "application/json",
			},
			{
				path: "layer.json",
				blob: new Blob(["layer"]),
				contentType: "application/json",
			},
		];
		mocks.collectSceneBundle.mockResolvedValue(files);
		mocks.initJob.mockResolvedValue({
			pipeline_id: "pipe-1",
			upload_prefix: "uploads/pipe-1",
			upload_expires_at: "later",
			upload_urls: {
				"scene.json": "https://upload/scene",
				"layer.json": "https://upload/layer",
			},
		});
		mocks.submitJob.mockResolvedValue({
			pipeline_id: "pipe-1",
			execution_name: "exec",
			scene_uri: "gs://bucket/scene.json",
		});

		await startCloudRender({
			scene,
			dir: asDirectoryHandle(MemoryDirectoryHandle.fromFiles({}, "scene-a")),
		});

		expect(mocks.signInWithGoogle).not.toHaveBeenCalled();
		expect(mocks.upsertJob).toHaveBeenCalledWith(
			expect.objectContaining({
				sceneName: "scene-a",
				status: "packaging",
				renderConfig: {
					width: 640,
					height: 360,
					minSamples: 4,
					maxSamples: 64,
					maxDepth: 6,
					integrator: "path_trace",
					startTime: 1,
					endTime: 3,
					fps: 24,
					startFrame: 24,
					endFrame: 72,
					isAnimation: true,
				},
			}),
		);
		expect(mocks.initJob).toHaveBeenCalledWith({
			files: [
				{ path: "scene.json", content_type: "application/json", size: 5 },
				{ path: "layer.json", content_type: "application/json", size: 5 },
			],
		});
		expect(fetch).toHaveBeenCalledTimes(2);
		expect(mocks.submitJob).toHaveBeenCalledWith("pipe-1", {
			enable_cache: true,
			stitch_fps: 24,
		});
		expect(mocks.addUploadedBytes).toHaveBeenNthCalledWith(1, "pipe-1", 5);
		expect(mocks.addUploadedBytes).toHaveBeenNthCalledWith(2, "pipe-1", 5);
		expect(mocks.jobs).toEqual([
			expect.objectContaining({
				id: "pipe-1",
				status: "succeeded",
				compositeName: "custom.png",
				compositeObjectURL: "blob:preview",
				stitchVideoURL: "https://signed/stitched.mp4",
				previewLoading: false,
				uploadedBytes: 10,
			}),
		]);
	});

	it("removes the local job and rethrows when bundling fails", async () => {
		mocks.getCurrentUser.mockReturnValue(null);
		mocks.collectSceneBundle.mockRejectedValue(
			new Error("Missing material texture"),
		);

		await expect(
			startCloudRender({
				scene: makeScene(),
				dir: asDirectoryHandle(MemoryDirectoryHandle.fromFiles({}, "broken")),
			}),
		).rejects.toThrow("Missing material texture");

		expect(mocks.signInWithGoogle).toHaveBeenCalledOnce();
		expect(mocks.removeJob).toHaveBeenCalledWith(
			expect.stringMatching(/^local-/),
		);
		expect(mocks.initJob).not.toHaveBeenCalled();
		expect(mocks.jobs).toEqual([]);
	});

	it("records API failures after upload on the promoted pipeline job instead of throwing", async () => {
		mocks.collectSceneBundle.mockResolvedValue([
			{
				path: "scene.json",
				blob: new Blob(["scene"]),
				contentType: "application/json",
			},
		]);
		mocks.initJob.mockResolvedValue({
			pipeline_id: "pipe-2",
			upload_prefix: "uploads/pipe-2",
			upload_expires_at: "later",
			upload_urls: { "scene.json": "https://upload/scene" },
		});
		mocks.submitJob.mockRejectedValue(new ApiError("quota exhausted", 429));

		await startCloudRender({
			scene: makeScene(),
			dir: asDirectoryHandle(MemoryDirectoryHandle.fromFiles({}, "scene-b")),
		});

		expect(mocks.jobs).toEqual([
			expect.objectContaining({
				id: "pipe-2",
				status: "failed",
				error: "quota exhausted",
				lastSyncError: undefined,
			}),
		]);
	});
});

function makeScene(): ResolvedScene {
	return {
		camera: {
			look_from: [0, 0, 5],
			look_at: [0, 0, 0],
			vup: [0, 1, 0],
			vfov: 45,
			aperture_radius: 0,
			focus_distance: 1,
		},
		contexts: [],
		layers: [],
		output_dir: "",
		animation: { start: 1, end: 3, fps: 24 },
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
