import type {
	CloudJob,
	CloudJobRenderConfig,
	CloudJobStatus,
} from "./cloud-job-types";

export type { CloudJob, CloudJobRenderConfig, CloudJobStatus };

const LS_KEY = "skewer.jobs.v1";
const MAX_JOBS = 20;

const terminal: Set<CloudJobStatus> = new Set([
	"succeeded",
	"failed",
	"cancelled",
]);

function isTerminal(s: CloudJobStatus): boolean {
	return terminal.has(s);
}

let jobs: CloudJob[] = (() => {
	try {
		const raw = localStorage.getItem(LS_KEY);
		if (!raw) return [];
		return JSON.parse(raw) as CloudJob[];
	} catch {
		return [];
	}
})();

const listeners = new Set<() => void>();

function persist() {
	const stripped = jobs.map((j) => {
		// Drop runtime-only fields
		// eslint-disable-next-line @typescript-eslint/no-unused-vars
		const { abort, compositeObjectURL, ...rest } = j;
		return rest;
	});
	try {
		localStorage.setItem(LS_KEY, JSON.stringify(stripped));
	} catch {
		// storage full, etc.
	}
}

function emit() {
	persist();
	for (const l of listeners) l();
}

function prune() {
	while (jobs.length > MAX_JOBS) {
		const terminals = jobs.filter((j) => isTerminal(j.status));
		if (terminals.length === 0) return;
		const drop = terminals.reduce((a, b) =>
			a.createdAt < b.createdAt ? a : b,
		);
		const i = jobs.findIndex((j) => j.id === drop.id);
		if (i < 0) return;
		const url = jobs[i]?.compositeObjectURL;
		if (url) URL.revokeObjectURL(url);
		jobs = jobs.filter((j) => j.id !== drop.id);
	}
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
	const i = jobs.findIndex((x) => x.id === j.id);
	if (i >= 0) jobs[i] = j;
	else {
		jobs = [j, ...jobs];
		prune();
	}
	emit();
}

export function patchJob(id: string, p: Partial<CloudJob>) {
	const i = jobs.findIndex((j) => j.id === id);
	if (i < 0) return;
	const prevStatus = jobs[i].status;
	const nextStatus = p.status ?? prevStatus;
	const stamp: Partial<CloudJob> = {};
	if (
		!isTerminal(prevStatus) &&
		isTerminal(nextStatus) &&
		jobs[i].completedAt === undefined
	) {
		stamp.completedAt = Date.now();
	}
	const nextId = p.id;
	if (nextId && nextId !== id) {
		const cur = { ...jobs[i], ...p, ...stamp, id: nextId };
		jobs.splice(i, 1);
		const j2 = jobs.findIndex((j) => j.id === nextId);
		if (j2 >= 0) jobs[j2] = cur;
		else jobs = [cur, ...jobs];
	} else {
		jobs[i] = { ...jobs[i], ...p, ...stamp };
	}
	prune();
	emit();
}

export function addUploadedBytes(id: string, delta: number) {
	const i = jobs.findIndex((j) => j.id === id);
	if (i < 0) return;
	jobs[i] = {
		...jobs[i],
		uploadedBytes: (jobs[i].uploadedBytes ?? 0) + delta,
	};
	emit();
}

export function removeJob(id: string) {
	const i = jobs.findIndex((j) => j.id === id);
	if (i < 0) return;
	if (jobs[i].compositeObjectURL) {
		URL.revokeObjectURL(jobs[i].compositeObjectURL);
	}
	jobs = jobs.filter((j) => j.id !== id);
	emit();
}

export function clearCompleted() {
	let changed = false;
	jobs = jobs.filter((j) => {
		if (!isTerminal(j.status)) return true;
		if (j.compositeObjectURL) URL.revokeObjectURL(j.compositeObjectURL);
		changed = true;
		return false;
	});
	if (changed) emit();
}

export function isNonTerminalStatus(s: CloudJobStatus): boolean {
	return !isTerminal(s);
}

export function isTerminalStatus(s: CloudJobStatus): boolean {
	return isTerminal(s);
}
