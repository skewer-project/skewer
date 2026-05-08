import type { ResolvedScene } from "../types/scene";
import {
	cancelJob,
	fetchCompositeBlob,
	getCompositeArtifactURL,
	getJobStatus,
	initJob,
	submitJob,
} from "./api/jobs";
import type { StatusResponse } from "./api/types";
import { ApiError } from "./api/types";
import { getCurrentUser, signInWithGoogle } from "./auth";
import { type BundleFile, collectSceneBundle } from "./bundle";
import type {
	CloudJob,
	CloudJobRenderConfig,
	CloudJobStatus,
} from "./cloud-job-types";
import * as store from "./jobs-store";

export type {
	CloudJob,
	CloudJobRenderConfig,
	CloudJobStatus,
} from "./cloud-job-types";

const UPLOAD_CONCURRENCY = 8;
const INITIAL_POLL_MS = 2000;
const MAX_POLL_MS = 10_000;
const COMPOSITE_FALLBACK = "frame-0001.png";
const STITCH_MP4 = "stitched.mp4";

const activePolls = new Map<string, AbortController>();

function sleep(ms: number) {
	return new Promise((r) => setTimeout(r, ms));
}

function isLocalJob(id: string): boolean {
	return id.startsWith("local-");
}

function compositePngName(status: StatusResponse): string {
	const u = status.composite_output;
	if (u) {
		const m = u.replace(/\\/g, "/").match(/\/([^/]+\.png)\s*$/i);
		if (m) return m[1] ?? COMPOSITE_FALLBACK;
	}
	return COMPOSITE_FALLBACK;
}

async function attachSucceededPreviews(
	pipelineId: string,
	pngName: string,
	signal: AbortSignal,
): Promise<void> {
	let stitchVideoURL: string | undefined;
	try {
		if (!signal.aborted) {
			stitchVideoURL = await getCompositeArtifactURL(pipelineId, STITCH_MP4);
		}
	} catch {
		/* stitch step disabled (stitch_fps == 0) */
	}
	try {
		if (signal.aborted) {
			return;
		}
		const pblob = await fetchCompositeBlob(pipelineId, pngName);
		const compositeObjectURL = URL.createObjectURL(pblob);
		store.patchJob(pipelineId, {
			status: "succeeded",
			compositeName: pngName,
			compositeObjectURL,
			lastSyncError: undefined,
			previewLoading: false,
			...(stitchVideoURL ? { stitchVideoURL } : {}),
		});
	} catch (e) {
		if (stitchVideoURL) {
			store.patchJob(pipelineId, {
				status: "succeeded",
				compositeName: pngName,
				stitchVideoURL,
				lastSyncError: undefined,
				previewLoading: false,
			});
			return;
		}
		throw e;
	}
}

async function runWithConcurrency<T, R>(
	items: T[],
	limit: number,
	fn: (item: T) => Promise<R>,
): Promise<void> {
	if (items.length === 0) return;
	const cap = Math.max(1, Math.min(limit, items.length));
	let next = 0;
	const runWorker = async () => {
		for (;;) {
			const i = next++;
			if (i >= items.length) return;
			await fn(items[i] as T);
		}
	};
	await Promise.all(Array.from({ length: cap }, () => runWorker()));
}

async function ensureSignedIn() {
	if (getCurrentUser()) return;
	await signInWithGoogle();
}

function jobExists(id: string): boolean {
	return store.getSnapshot().some((j) => j.id === id);
}

function patchIfExists(id: string, patch: Partial<CloudJob>) {
	if (jobExists(id)) {
		store.patchJob(id, patch);
	}
}

function markFailed(id: string, message: string) {
	patchIfExists(id, {
		status: "failed" as CloudJobStatus,
		error: message,
		lastSyncError: undefined,
	});
}

function markSyncError(id: string, message: string) {
	patchIfExists(id, { lastSyncError: message });
}

function markSynced(id: string, patch: Partial<CloudJob>) {
	patchIfExists(id, {
		...patch,
		lastSyncedAt: Date.now(),
		lastSyncError: undefined,
	});
}

async function refetchCompositePreview(id: string, name?: string) {
	const job = store.getSnapshot().find((j) => j.id === id);
	const artifactName = name ?? job?.compositeName ?? COMPOSITE_FALLBACK;
	store.patchJob(id, { lastSyncError: undefined, previewLoading: true });
	try {
		await attachSucceededPreviews(
			id,
			artifactName,
			new AbortController().signal,
		);
	} catch (e) {
		store.patchJob(id, { previewLoading: false });
		markSyncError(
			id,
			`Composite preview unavailable: ${e instanceof Error ? e.message : String(e)}`,
		);
	}
}

async function reconcileJobStatus(
	pipelineId: string,
	signal: AbortSignal,
): Promise<boolean> {
	let st: Awaited<ReturnType<typeof getJobStatus>>;
	try {
		st = await getJobStatus(pipelineId);
	} catch (e) {
		markSyncError(pipelineId, e instanceof Error ? e.message : String(e));
		return false;
	}

	if (signal.aborted) return true;

	if (st === "pending") {
		markSynced(pipelineId, { status: "submitting" });
		return false;
	}

	const s = st.status;
	if (s === "PIPELINE_STATUS_RUNNING" || s === "PIPELINE_STATUS_UNSPECIFIED") {
		markSynced(pipelineId, { status: "running", error: undefined });
		return false;
	}
	if (s === "PIPELINE_STATUS_SUCCEEDED") {
		const name = compositePngName(st);
		markSynced(pipelineId, {
			status: "succeeded",
			compositeName: name,
			error: undefined,
		});
		if (!signal.aborted) {
			await refetchCompositePreview(pipelineId, name);
		}
		return true;
	}
	if (s === "PIPELINE_STATUS_FAILED") {
		markSynced(pipelineId, {
			status: "failed",
			error: st.error_message || "Render failed",
		});
		return true;
	}
	if (s === "PIPELINE_STATUS_CANCELLED") {
		markSynced(pipelineId, { status: "cancelled", error: undefined });
		return true;
	}

	markSyncError(pipelineId, `Unknown pipeline status: ${s}`);
	return false;
}

async function pollUntilDone(
	pipelineId: string,
	signal: AbortSignal,
): Promise<void> {
	let delay = INITIAL_POLL_MS;
	for (;;) {
		if (signal.aborted) return;
		const done = await reconcileJobStatus(pipelineId, signal);
		if (done || signal.aborted) return;
		await sleep(delay);
		delay = Math.min(MAX_POLL_MS, Math.floor(delay * 1.4));
	}
}

function startStatusPoll(
	pipelineId: string,
	ac = new AbortController(),
	force = false,
) {
	if (isLocalJob(pipelineId)) return;
	const existing = activePolls.get(pipelineId);
	if (existing && !existing.signal.aborted) {
		if (!force) return;
		existing.abort();
	}
	activePolls.set(pipelineId, ac);
	store.patchJob(pipelineId, { abort: ac });
	void pollUntilDone(pipelineId, ac.signal).finally(() => {
		if (activePolls.get(pipelineId) === ac) {
			activePolls.delete(pipelineId);
		}
	});
}

async function pollTracked(
	pipelineId: string,
	ac: AbortController,
): Promise<void> {
	if (isLocalJob(pipelineId)) return;
	activePolls.set(pipelineId, ac);
	store.patchJob(pipelineId, { abort: ac });
	try {
		await pollUntilDone(pipelineId, ac.signal);
	} finally {
		if (activePolls.get(pipelineId) === ac) {
			activePolls.delete(pipelineId);
		}
	}
}

export async function startCloudRender(args: {
	scene: ResolvedScene;
	dir: FileSystemDirectoryHandle;
	enableCache?: boolean;
	renderConfig?: CloudJobRenderConfig;
}): Promise<void> {
	const { scene, dir, enableCache = true, renderConfig } = args;
	await ensureSignedIn();

	const localId = `local-${crypto.randomUUID()}`;
	const ac = new AbortController();
	let activeId = localId;

	const baseJob: CloudJob = {
		id: localId,
		createdAt: Date.now(),
		sceneName: dir.name,
		status: "packaging",
		abort: ac,
		renderConfig,
	};
	store.upsertJob(baseJob);

	const fail = (message: string) => {
		markFailed(activeId, message);
	};

	let inBundlePhase = true;
	try {
		const bundle = await collectSceneBundle(scene, dir);
		inBundlePhase = false;
		if (ac.signal.aborted) {
			store.removeJob(activeId);
			return;
		}
		const totalBytes = bundle.reduce((s, f) => s + f.blob.size, 0);
		store.patchJob(activeId, {
			status: "uploading-init",
			totalFiles: bundle.length,
			totalBytes,
			uploadedBytes: 0,
			lastSyncError: undefined,
		});

		const init = await initJob({
			files: bundle.map(({ path, contentType, blob }) => ({
				path,
				content_type: contentType,
				size: blob.size,
			})),
		});
		if (ac.signal.aborted) return;

		const pipelineId = init.pipeline_id;
		store.patchJob(activeId, {
			id: pipelineId,
			status: "uploading",
			lastSyncError: undefined,
		});
		activeId = pipelineId;

		await runWithConcurrency(
			bundle,
			UPLOAD_CONCURRENCY,
			async (f: BundleFile) => {
				if (ac.signal.aborted) {
					throw new DOMException("aborted", "AbortError");
				}
				const url = init.upload_urls[f.path];
				if (!url) {
					throw new Error(`Missing signed URL for path ${f.path}`);
				}
				const res = await fetch(url, {
					method: "PUT",
					body: f.blob,
					headers: { "Content-Type": f.contentType },
					signal: ac.signal,
				});
				if (!res.ok) {
					throw new Error(`PUT ${f.path} failed: ${res.status}`);
				}
				store.addUploadedBytes(activeId, f.blob.size);
			},
		);

		if (ac.signal.aborted) return;
		store.patchJob(activeId, {
			status: "submitting",
			lastSyncError: undefined,
		});
		const stitchFps =
			renderConfig?.isAnimation === true && (renderConfig.fps ?? 0) > 0
				? renderConfig.fps
				: 0;
		await submitJob(activeId, {
			enable_cache: enableCache,
			stitch_fps: stitchFps,
		});
		if (ac.signal.aborted) return;
		store.patchJob(activeId, { status: "running" });
		await pollTracked(activeId, ac);
	} catch (e) {
		if (e instanceof DOMException && e.name === "AbortError") {
			store.removeJob(activeId);
			return;
		}
		if (inBundlePhase) {
			store.removeJob(activeId);
			throw e;
		}
		if (e instanceof ApiError) {
			fail(e.message);
			return;
		}
		const current = store.getSnapshot().find((j) => j.id === activeId);
		if (current?.status === "submitting" && !isLocalJob(activeId)) {
			markSyncError(
				activeId,
				`Submit result unknown: ${e instanceof Error ? e.message : String(e)}`,
			);
			startStatusPoll(activeId, ac, true);
			return;
		}
		fail(e instanceof Error ? e.message : String(e));
	}
}

export async function userCancelRender(id: string) {
	const j = store.getSnapshot().find((x) => x.id === id);
	j?.abort?.abort();
	try {
		await cancelJob(id);
	} catch {
		markSyncError(id, "Cancel request failed; refreshing server status.");
		startStatusPoll(id, new AbortController(), true);
		return;
	}
	if (jobExists(id)) {
		store.patchJob(id, { status: "cancelled", lastSyncError: undefined });
	}
}

export async function resumePendingJobs() {
	await store.whenHydrated();
	for (const j of store.getSnapshot()) {
		if (isLocalJob(j.id) && store.isNonTerminalStatus(j.status)) {
			store.patchJob(j.id, {
				status: "failed",
				error: "Client restarted before job was created on the server.",
			});
			continue;
		}
		if (isLocalJob(j.id)) {
			continue;
		}
		if (
			j.status === "packaging" ||
			j.status === "uploading-init" ||
			j.status === "uploading"
		) {
			store.patchJob(j.id, {
				status: "failed",
				error: "Upload did not complete before reload. Start a new render.",
			});
			continue;
		}
		if (j.status === "submitting" || j.status === "running") {
			startStatusPoll(j.id);
			continue;
		}
		if (
			j.status === "succeeded" &&
			!j.compositeObjectURL &&
			!j.stitchVideoURL
		) {
			void refetchCompositePreview(j.id);
		}
	}
}

export function refreshCloudJob(id: string) {
	const job = store.getSnapshot().find((j) => j.id === id);
	if (!job || isLocalJob(id)) return;
	if (job.status === "succeeded") {
		void refetchCompositePreview(id);
		return;
	}
	if (store.isNonTerminalStatus(job.status)) {
		startStatusPoll(id, new AbortController(), true);
		return;
	}
	const ac = new AbortController();
	void reconcileJobStatus(id, ac.signal);
}

export function markStitchPreviewUnavailable(id: string) {
	store.patchJob(id, { stitchVideoURL: undefined });
}

export function refreshCloudJobs() {
	for (const job of store.getSnapshot()) {
		if (isLocalJob(job.id)) continue;
		if (
			job.status === "succeeded" &&
			!job.compositeObjectURL &&
			!job.stitchVideoURL
		) {
			void refetchCompositePreview(job.id);
			continue;
		}
		if (store.isNonTerminalStatus(job.status)) {
			startStatusPoll(job.id, new AbortController(), true);
		}
	}
}

export async function downloadCompositePng(
	pipelineId: string,
	suggestedName: string,
) {
	const job = store.getSnapshot().find((j) => j.id === pipelineId);
	let blob: Blob;
	let name = suggestedName;
	try {
		blob = await fetchCompositeBlob(pipelineId, STITCH_MP4);
		name = suggestedName.replace(/\.png$/i, ".mp4");
	} catch {
		blob = await fetchCompositeBlob(
			pipelineId,
			job?.compositeName ?? COMPOSITE_FALLBACK,
		);
	}
	const url = URL.createObjectURL(blob);
	const a = document.createElement("a");
	a.href = url;
	a.download = name;
	a.click();
	URL.revokeObjectURL(url);
}
