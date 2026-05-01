import type { ResolvedScene } from "../types/scene";
import {
	cancelJob,
	fetchCompositeBlob,
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
export { isNonTerminalStatus } from "./jobs-store";

const UPLOAD_CONCURRENCY = 8;
const MAX_POLL_MS = 10_000;
const COMPOSITE_FALLBACK = "frame-0001.png";
const SMEAR_MP4 = "smeared.mp4";

function sleep(ms: number) {
	return new Promise((r) => setTimeout(r, ms));
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
	let smearObjectURL: string | undefined;
	try {
		if (!signal.aborted) {
			const vblob = await fetchCompositeBlob(pipelineId, SMEAR_MP4);
			smearObjectURL = URL.createObjectURL(vblob);
		}
	} catch {
		/* smear step off or still writing */
	}
	try {
		if (signal.aborted) {
			return;
		}
		const pblob = await fetchCompositeBlob(pipelineId, pngName);
		const compositeObjectURL = URL.createObjectURL(pblob);
		store.patchJob(pipelineId, {
			status: "succeeded",
			compositeObjectURL,
			...(smearObjectURL ? { smearObjectURL } : {}),
		});
	} catch (e) {
		if (smearObjectURL) {
			store.patchJob(pipelineId, { status: "succeeded", smearObjectURL });
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

function markFailed(id: string, message: string) {
	const exists = store.getSnapshot().some((j) => j.id === id);
	if (exists) {
		store.patchJob(id, { status: "failed" as CloudJobStatus, error: message });
	}
}

async function pollUntilDone(
	pipelineId: string,
	signal: AbortSignal,
): Promise<void> {
	let delay = 2000;
	for (;;) {
		if (signal.aborted) {
			return;
		}
		let st: Awaited<ReturnType<typeof getJobStatus>>;
		try {
			st = await getJobStatus(pipelineId);
		} catch (e) {
			markFailed(pipelineId, e instanceof Error ? e.message : String(e));
			return;
		}
		if (st === "pending") {
			store.patchJob(pipelineId, { status: "running" });
		} else {
			const s = st.status;
			if (
				s === "PIPELINE_STATUS_RUNNING" ||
				s === "PIPELINE_STATUS_UNSPECIFIED"
			) {
				store.patchJob(pipelineId, { status: "running" });
			} else if (s === "PIPELINE_STATUS_SUCCEEDED") {
				const name = compositePngName(st);
				try {
					if (signal.aborted) {
						return;
					}
					await attachSucceededPreviews(pipelineId, name, signal);
				} catch (e) {
					markFailed(
						pipelineId,
						`Composite download failed: ${e instanceof Error ? e.message : String(e)}`,
					);
				}
				return;
			} else if (s === "PIPELINE_STATUS_FAILED") {
				store.patchJob(pipelineId, {
					status: "failed",
					error: st.error_message || "Render failed",
				});
				return;
			} else if (s === "PIPELINE_STATUS_CANCELLED") {
				store.patchJob(pipelineId, { status: "cancelled" });
				return;
			}
		}
		const wait = signal.aborted ? 0 : delay;
		if (wait > 0) await sleep(wait);
		if (signal.aborted) return;
		delay = Math.min(MAX_POLL_MS, Math.floor(delay * 1.4));
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
		store.patchJob(activeId, { status: "submitting" });
		const smearFps =
			renderConfig?.isAnimation === true && (renderConfig.fps ?? 0) > 0
				? renderConfig.fps
				: 0;
		await submitJob(activeId, {
			enable_cache: enableCache,
			smear_fps: smearFps,
		});
		if (ac.signal.aborted) return;
		store.patchJob(activeId, { status: "running" });
		await pollUntilDone(activeId, ac.signal);
	} catch (e) {
		if (e instanceof ApiError) {
			fail(e.message);
			return;
		}
		if (e instanceof DOMException && e.name === "AbortError") {
			store.removeJob(activeId);
			return;
		}
		if (inBundlePhase) {
			store.removeJob(activeId);
			throw e;
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
		// not submitted, or already ended
	}
	if (store.getSnapshot().some((x) => x.id === id)) {
		store.patchJob(id, { status: "cancelled" });
	}
}

async function refetchCompositePreview(id: string) {
	try {
		await attachSucceededPreviews(id, COMPOSITE_FALLBACK, new AbortController().signal);
	} catch {
		// keep card without image
	}
}

export function resumePendingJobs() {
	for (const j of store.getSnapshot()) {
		if (j.id.startsWith("local-") && store.isNonTerminalStatus(j.status)) {
			store.patchJob(j.id, {
				status: "failed",
				error: "Client restarted before job was created on the server.",
			});
			continue;
		}
		if (j.id.startsWith("local-")) {
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
			const ac = new AbortController();
			store.patchJob(j.id, { abort: ac });
			void pollUntilDone(j.id, ac.signal);
			continue;
		}
		if (j.status === "succeeded" && !j.compositeObjectURL && !j.smearObjectURL) {
			void refetchCompositePreview(j.id);
		}
	}
}

export async function downloadCompositePng(
	pipelineId: string,
	suggestedName: string,
) {
	let blob: Blob;
	let name = suggestedName;
	try {
		blob = await fetchCompositeBlob(pipelineId, SMEAR_MP4);
		name = suggestedName.replace(/\.png$/i, ".mp4");
	} catch {
		blob = await fetchCompositeBlob(pipelineId, COMPOSITE_FALLBACK);
	}
	const url = URL.createObjectURL(blob);
	const a = document.createElement("a");
	a.href = url;
	a.download = name;
	a.click();
	URL.revokeObjectURL(url);
}
