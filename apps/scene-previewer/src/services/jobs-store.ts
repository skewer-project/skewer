import type {
	CloudJob,
	CloudJobClientStatus,
	CloudJobRenderConfig,
	CloudJobStatus,
} from "./cloud-job-types";
import { CLOUD_JOBS_STORE, openPreviewerDB } from "./previewer-db";

export type {
	CloudJob,
	CloudJobClientStatus,
	CloudJobRenderConfig,
	CloudJobStatus,
};

const LEGACY_LS_KEY = "skewer.jobs.v1";
const MAX_JOBS = 20;

type PersistedCloudJob = Omit<CloudJob, "abort" | "compositeObjectURL">;

const terminal: Set<CloudJobStatus> = new Set([
	"succeeded",
	"failed",
	"cancelled",
]);

const clientOwned: Set<CloudJobStatus> = new Set([
	"packaging",
	"uploading-init",
	"uploading",
	"submitting",
]);

function isTerminal(s: CloudJobStatus): boolean {
	return terminal.has(s);
}

function sortJobs(list: CloudJob[]): CloudJob[] {
	return [...list].sort((a, b) => b.createdAt - a.createdAt);
}

function stripRuntimeFields(j: CloudJob): PersistedCloudJob {
	const { abort, compositeObjectURL, ...rest } = j;
	void abort;
	void compositeObjectURL;
	return rest;
}

function revokeObjectURL(j: CloudJob | undefined) {
	if (j?.compositeObjectURL) {
		URL.revokeObjectURL(j.compositeObjectURL);
	}
}

function pruneList(list: CloudJob[]): CloudJob[] {
	let next = sortJobs(list);
	while (next.length > MAX_JOBS) {
		const terminals = next.filter((j) => isTerminal(j.status));
		if (terminals.length === 0) return next;
		const drop = terminals.reduce((a, b) =>
			a.createdAt < b.createdAt ? a : b,
		);
		revokeObjectURL(next.find((j) => j.id === drop.id));
		next = next.filter((j) => j.id !== drop.id);
	}
	return next;
}

function normalizeJob(j: CloudJob): CloudJob {
	return { ...j };
}

let jobs: CloudJob[] = [];
let hydrated = false;
let pendingPersist = false;
let persistInFlight = false;
const listeners = new Set<() => void>();

function emit() {
	for (const l of listeners) l();
}

function publish(nextJobs: CloudJob[], persist = true) {
	jobs = pruneList(nextJobs).map(normalizeJob);
	emit();
	if (persist && hydrated) {
		schedulePersist();
	}
}

function requestAll<T>(store: IDBObjectStore): Promise<T[]> {
	return new Promise((resolve, reject) => {
		const req = store.getAll();
		req.onsuccess = () => resolve(req.result as T[]);
		req.onerror = () => reject(req.error);
	});
}

async function readPersistedJobs(): Promise<CloudJob[]> {
	const db = await openPreviewerDB();
	const tx = db.transaction(CLOUD_JOBS_STORE, "readonly");
	const store = tx.objectStore(CLOUD_JOBS_STORE);
	const rows = await requestAll<PersistedCloudJob>(store);
	return rows.map(normalizeJob);
}

async function writePersistedJobs(snapshot: CloudJob[]): Promise<void> {
	const db = await openPreviewerDB();
	const tx = db.transaction(CLOUD_JOBS_STORE, "readwrite");
	const store = tx.objectStore(CLOUD_JOBS_STORE);
	store.clear();
	for (const job of snapshot.map(stripRuntimeFields)) {
		store.put(job);
	}
	await new Promise<void>((resolve, reject) => {
		tx.oncomplete = () => resolve();
		tx.onerror = () => reject(tx.error);
		tx.onabort = () => reject(tx.error);
	});
}

function readLegacyJobs(): CloudJob[] {
	try {
		const raw = localStorage.getItem(LEGACY_LS_KEY);
		if (!raw) return [];
		const parsed = JSON.parse(raw) as CloudJob[];
		return Array.isArray(parsed)
			? parsed.map((j) => stripRuntimeFields(j))
			: [];
	} catch {
		return [];
	}
}

function clearLegacyJobs() {
	try {
		localStorage.removeItem(LEGACY_LS_KEY);
	} catch {
		// noop
	}
}

function mergeJobs(preferred: CloudJob[], fallback: CloudJob[]): CloudJob[] {
	const byId = new Map<string, CloudJob>();
	for (const job of fallback) {
		byId.set(job.id, normalizeJob(job));
	}
	for (const job of preferred) {
		byId.set(job.id, normalizeJob(job));
	}
	return pruneList([...byId.values()]);
}

function schedulePersist() {
	pendingPersist = true;
	if (!persistInFlight) {
		void flushPersist();
	}
}

async function flushPersist() {
	persistInFlight = true;
	try {
		while (pendingPersist) {
			pendingPersist = false;
			const snapshot = jobs.map(normalizeJob);
			await writePersistedJobs(snapshot);
		}
	} catch {
		// Job history is a cache; keep the in-memory state authoritative for UI.
	} finally {
		persistInFlight = false;
		if (pendingPersist) {
			void flushPersist();
		}
	}
}

async function hydrateJobs() {
	const legacy = readLegacyJobs();
	try {
		const persisted = await readPersistedJobs();
		hydrated = true;
		jobs = mergeJobs(jobs, mergeJobs(persisted, legacy));
		emit();
		schedulePersist();
		clearLegacyJobs();
	} catch {
		hydrated = true;
		if (legacy.length > 0) {
			jobs = mergeJobs(jobs, legacy);
			emit();
			schedulePersist();
		}
	}
}

const hydrationPromise = hydrateJobs();

export function whenHydrated(): Promise<void> {
	return hydrationPromise;
}

export function subscribe(fn: () => void) {
	listeners.add(fn);
	return () => {
		listeners.delete(fn);
	};
}

export function getSnapshot(): CloudJob[] {
	return jobs;
}

export function upsertJob(j: CloudJob) {
	const nextJob = normalizeJob(j);
	const exists = jobs.some((x) => x.id === j.id);
	const next = exists
		? jobs.map((x) => (x.id === j.id ? nextJob : x))
		: [nextJob, ...jobs];
	publish(next);
}

export function patchJob(id: string, p: Partial<CloudJob>) {
	const i = jobs.findIndex((j) => j.id === id);
	if (i < 0) return;

	const prev = jobs[i];
	const prevStatus = prev.status;
	const nextStatus = p.status ?? prevStatus;
	const stamp: Partial<CloudJob> = {};
	if (
		!isTerminal(prevStatus) &&
		isTerminal(nextStatus) &&
		prev.completedAt === undefined
	) {
		stamp.completedAt = Date.now();
	}

	const nextId = p.id && p.id !== id ? p.id : id;
	if (
		p.compositeObjectURL &&
		prev.compositeObjectURL &&
		p.compositeObjectURL !== prev.compositeObjectURL
	) {
		URL.revokeObjectURL(prev.compositeObjectURL);
	}
	const nextJob = normalizeJob({ ...prev, ...p, ...stamp, id: nextId });
	let next = jobs.filter((j) => j.id !== id);
	const existing = next.findIndex((j) => j.id === nextId);
	if (existing >= 0) {
		next = next.map((j) => (j.id === nextId ? nextJob : j));
	} else {
		next = [nextJob, ...next];
	}
	publish(next);
}

export function addUploadedBytes(id: string, delta: number) {
	const next = jobs.map((j) =>
		j.id === id
			? {
					...j,
					uploadedBytes: (j.uploadedBytes ?? 0) + delta,
				}
			: j,
	);
	publish(next);
}

export function removeJob(id: string) {
	const existing = jobs.find((j) => j.id === id);
	if (!existing) return;
	revokeObjectURL(existing);
	publish(
		jobs.filter((j) => j.id !== id),
		true,
	);
}

export function clearCompleted() {
	const completed = jobs.filter((j) => isTerminal(j.status));
	if (completed.length === 0) return;
	for (const job of completed) {
		revokeObjectURL(job);
	}
	publish(
		jobs.filter((j) => !isTerminal(j.status)),
		true,
	);
}

export function isNonTerminalStatus(s: CloudJobStatus): boolean {
	return !isTerminal(s);
}

export function isTerminalStatus(s: CloudJobStatus): boolean {
	return isTerminal(s);
}

export function isClientOwnedStatus(s: CloudJobStatus): boolean {
	return clientOwned.has(s);
}
