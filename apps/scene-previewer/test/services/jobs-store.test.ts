import { beforeEach, describe, expect, it, vi } from "vitest";
import type { CloudJob } from "../../src/services/cloud-job-types";

interface FakeTx {
	oncomplete: (() => void) | null;
	onerror: (() => void) | null;
	onabort: (() => void) | null;
	error: Error | null;
	objectStore: (name: string) => {
		getAll: () => {
			result?: unknown[];
			error?: Error;
			onsuccess: (() => void) | null;
			onerror: (() => void) | null;
		};
		clear: () => void;
		put: (value: unknown) => void;
	};
}

function makeJob(id: string, overrides: Partial<CloudJob> = {}): CloudJob {
	return {
		id,
		createdAt: Number(id.replace(/\D/g, "")) || Date.now(),
		sceneName: `scene-${id}`,
		status: "running",
		...overrides,
	};
}

function makeDb(initialRows: unknown[] = []) {
	let rows = [...initialRows];
	const writes: unknown[][] = [];

	return {
		get rows() {
			return rows;
		},
		get writes() {
			return writes;
		},
		openPreviewerDB: vi.fn(async () => ({
			transaction: (_storeName: string, mode: "readonly" | "readwrite") => {
				const staged: unknown[] = [];
				const tx: FakeTx = {
					oncomplete: null,
					onerror: null,
					onabort: null,
					error: null,
					objectStore: () => ({
						getAll: () => {
							const req = {
								result: rows,
								error: undefined,
								onsuccess: null,
								onerror: null,
							};
							queueMicrotask(() => req.onsuccess?.());
							return req;
						},
						clear: () => {
							staged.length = 0;
						},
						put: (value: unknown) => {
							staged.push(structuredClone(value));
						},
					}),
				};

				if (mode === "readwrite") {
					queueMicrotask(() => {
						rows = [...staged];
						writes.push(structuredClone(staged));
						tx.oncomplete?.();
					});
				}

				return tx;
			},
		})),
	};
}

function installLocalStorage() {
	const storage = new Map<string, string>();
	vi.stubGlobal("localStorage", {
		getItem: vi.fn((key: string) => storage.get(key) ?? null),
		setItem: vi.fn((key: string, value: string) => {
			storage.set(key, value);
		}),
		removeItem: vi.fn((key: string) => {
			storage.delete(key);
		}),
		clear: vi.fn(() => {
			storage.clear();
		}),
	});
}

async function loadStore({
	persisted = [],
	legacy = [],
}: {
	persisted?: unknown[];
	legacy?: unknown[];
} = {}) {
	vi.resetModules();
	installLocalStorage();
	localStorage.clear();
	if (legacy.length > 0) {
		localStorage.setItem("skewer.jobs.v1", JSON.stringify(legacy));
	}

	const db = makeDb(persisted);
	vi.doMock("../../src/services/previewer-db", () => ({
		CLOUD_JOBS_STORE: "cloud-jobs",
		openPreviewerDB: db.openPreviewerDB,
	}));

	const store = await import("../../src/services/jobs-store");
	await store.whenHydrated();
	return { store, db };
}

describe("jobs-store", () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		installLocalStorage();
		localStorage.clear();
	});

	it("hydrates from IndexedDB and legacy storage and normalizes missing previews", async () => {
		const persisted = [
			makeJob("persisted-1", {
				status: "succeeded",
				renderConfig: {
					width: 1,
					height: 1,
					maxSamples: 2,
					maxDepth: 3,
					integrator: "path_trace",
					startTime: 0,
					endTime: 0,
					fps: 24,
					startFrame: 0,
					endFrame: 0,
					isAnimation: false,
				},
			}),
		];
		const legacy = [
			makeJob("legacy-1", {
				status: "failed",
				compositeObjectURL: "blob:legacy-preview",
				stitchVideoURL: "https://example/video.mp4",
				previewLoading: false,
				abort: new AbortController(),
			}),
		];

		const { store, db } = await loadStore({ persisted, legacy });

		expect(store.getSnapshot()).toEqual(
			expect.arrayContaining([
				expect.objectContaining({
					id: "persisted-1",
					status: "succeeded",
					previewLoading: true,
				}),
				expect.objectContaining({
					id: "legacy-1",
					status: "failed",
				}),
			]),
		);
		const legacyJob = store.getSnapshot().find((job) => job.id === "legacy-1");
		expect(legacyJob).not.toHaveProperty("abort");
		expect(legacyJob).not.toHaveProperty("compositeObjectURL");
		expect(legacyJob).not.toHaveProperty("stitchVideoURL");
		expect(localStorage.getItem("skewer.jobs.v1")).toBeNull();
		expect(db.writes.at(-1)).toEqual([
			expect.not.objectContaining({
				abort: expect.anything(),
				compositeObjectURL: expect.anything(),
				stitchVideoURL: expect.anything(),
				previewLoading: expect.anything(),
			}),
			expect.not.objectContaining({
				abort: expect.anything(),
				compositeObjectURL: expect.anything(),
				stitchVideoURL: expect.anything(),
				previewLoading: expect.anything(),
			}),
		]);
	});

	it("prunes the oldest terminal jobs when the list exceeds capacity", async () => {
		const revoked: string[] = [];
		vi.spyOn(URL, "revokeObjectURL").mockImplementation((url: string) => {
			revoked.push(url);
		});

		const { store } = await loadStore();
		for (let i = 0; i < 20; i++) {
			store.upsertJob(
				makeJob(`terminal-${i}`, {
					createdAt: i,
					status: "succeeded",
					compositeObjectURL: `blob:terminal-${i}`,
				}),
			);
		}
		store.upsertJob(
			makeJob("active", {
				createdAt: 100,
				status: "running",
				compositeObjectURL: "blob:active",
			}),
		);

		expect(store.getSnapshot()).toHaveLength(20);
		expect(store.getSnapshot().some((job) => job.id === "active")).toBe(true);
		expect(store.getSnapshot().some((job) => job.id === "terminal-0")).toBe(
			false,
		);
		expect(revoked).toEqual(["blob:terminal-0"]);
	});

	it("timestamps first terminal transition, merges renamed jobs, and revokes replaced preview URLs", async () => {
		vi.useFakeTimers();
		vi.setSystemTime(new Date("2026-05-28T12:00:00Z"));
		const revokeObjectURL = vi
			.spyOn(URL, "revokeObjectURL")
			.mockImplementation(() => {});

		const { store } = await loadStore();
		store.upsertJob(
			makeJob("local-1", {
				createdAt: 1,
				status: "running",
				compositeObjectURL: "blob:old-preview",
			}),
		);

		store.patchJob("local-1", {
			id: "pipeline-1",
			status: "succeeded",
			compositeObjectURL: "blob:new-preview",
		});
		store.patchJob("pipeline-1", { status: "failed" });

		expect(store.getSnapshot()).toEqual([
			expect.objectContaining({
				id: "pipeline-1",
				status: "failed",
				completedAt: Date.now(),
				compositeObjectURL: "blob:new-preview",
			}),
		]);
		expect(revokeObjectURL).toHaveBeenCalledWith("blob:old-preview");
	});
});
