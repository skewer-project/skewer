import {
	Cloud,
	CloudOff,
	Download,
	Loader2,
	RefreshCw,
	Trash2,
	X,
} from "lucide-react";
import { useCallback, useMemo, useState, useSyncExternalStore } from "react";
import { createPortal } from "react-dom";
import type {
	CloudJob,
	CloudJobRenderConfig,
} from "../services/cloud-job-types";
import {
	downloadCompositePng,
	startCloudRender,
	userCancelRender,
} from "../services/cloud-render";
import {
	clearCompleted,
	getSnapshot,
	isNonTerminalStatus,
	isTerminalStatus,
	removeJob,
	subscribe,
} from "../services/jobs-store";
import u from "../styles/shared/uiPrimitives.module.css";
import type { ResolvedScene } from "../types/scene";
import j from "./JobsModal.module.css";

type Filter = "all" | "running" | "done" | "failed";

function useJobs() {
	return useSyncExternalStore(subscribe, getSnapshot, getSnapshot);
}

function statusLabel(j: CloudJob): string {
	switch (j.status) {
		case "packaging":
			return "Preparing bundle…";
		case "uploading-init":
			return "Reserving upload…";
		case "uploading":
			return "Uploading assets…";
		case "submitting":
			return "Submitting job…";
		case "running":
			return "Rendering on cloud…";
		case "succeeded":
			return "Done";
		case "failed":
			return "Failed";
		case "cancelled":
			return "Cancelled";
		default:
			return j.status;
	}
}

function statusTone(j: CloudJob): "active" | "ok" | "warn" | "err" {
	if (j.status === "succeeded") return "ok";
	if (j.status === "failed") return "err";
	if (j.status === "cancelled") return "warn";
	return "active";
}

function relativeTime(ts: number): string {
	const ms = Date.now() - ts;
	const s = Math.floor(ms / 1000);
	if (s < 5) return "just now";
	if (s < 60) return `${s}s ago`;
	const m = Math.floor(s / 60);
	if (m < 60) return `${m}m ago`;
	const h = Math.floor(m / 60);
	if (h < 24) return `${h}h ago`;
	const d = Math.floor(h / 24);
	return `${d}d ago`;
}

function settingsLine(rc: CloudJobRenderConfig): string {
	const parts = [
		`${rc.width}×${rc.height}`,
		`${rc.minSamples ?? 16}–${rc.maxSamples}spp`,
		`d${rc.maxDepth}`,
	];
	if (rc.isAnimation) {
		parts.push(`frames ${rc.startFrame}–${rc.endFrame}`);
		parts.push(`${rc.fps}fps`);
	} else {
		parts.push(`frame ${rc.startFrame}`);
	}
	return parts.join("  ·  ");
}

function JobRow({
	job,
	expanded,
	onToggleExpand,
	onRetry,
	onDownload,
	onCancel,
	onRemove,
	canRetry,
}: {
	job: CloudJob;
	expanded: boolean;
	onToggleExpand: () => void;
	onRetry: () => void;
	onDownload: () => void;
	onCancel: () => void;
	onRemove: () => void;
	canRetry: boolean;
}) {
	const tone = statusTone(job);
	const running = isNonTerminalStatus(job.status);
	const pct =
		job.totalBytes && (job.uploadedBytes ?? 0) > 0
			? Math.min(100, ((job.uploadedBytes ?? 0) / job.totalBytes) * 100)
			: 0;
	const stamp =
		job.completedAt !== undefined
			? relativeTime(job.completedAt)
			: relativeTime(job.createdAt);

	const rowMod =
		tone === "active"
			? j.rowActive
			: tone === "err"
				? j.rowErr
				: tone === "warn"
					? j.rowWarn
					: "";

	const statusClass =
		tone === "active"
			? j.statusActive
			: tone === "ok"
				? j.statusOk
				: tone === "err"
					? j.statusErr
					: j.statusWarn;

	const thumb = job.compositeObjectURL ? (
		<img className={j.thumbImg} src={job.compositeObjectURL} alt="" />
	) : running ? (
		<Loader2 className={j.thumbSpin} size={20} />
	) : tone === "err" ? (
		<CloudOff size={20} aria-hidden />
	) : (
		<Cloud size={20} aria-hidden />
	);

	return (
		<div className={rowMod ? `${j.row} ${rowMod}` : j.row}>
			<div className={j.thumb}>{thumb}</div>
			<div className={j.rowMain}>
				<div className={j.rowHead}>
					<span className={j.rowTitle}>{job.sceneName}</span>
					<span className={`${j.rowStatus} ${statusClass}`}>
						{statusLabel(job)}
					</span>
				</div>
				<div className={j.rowMeta}>
					<span className={j.stamp}>{stamp}</span>
					{job.renderConfig ? (
						<>
							<span className={j.metaSep}>·</span>
							<span className={j.rowSettings}>
								{settingsLine(job.renderConfig)}
							</span>
						</>
					) : null}
				</div>
				{job.status === "uploading" && job.totalBytes ? (
					<div
						className={j.progress}
						role="progressbar"
						aria-label="Upload progress"
					>
						<div
							className={j.progressBar}
							style={{ width: `${pct.toFixed(1)}%` }}
						/>
					</div>
				) : null}
				{job.error ? (
					<div className={j.rowError} role="alert">
						{job.error}
					</div>
				) : null}
				{expanded && job.renderConfig ? (
					<div className={j.detail}>
						<div className={j.detailRow}>
							<span className={j.detailK}>resolution</span>
							<span className={j.detailV}>
								{job.renderConfig.width} × {job.renderConfig.height}
							</span>
						</div>
						<div className={j.detailRow}>
							<span className={j.detailK}>samples</span>
							<span className={j.detailV}>
								{job.renderConfig.minSamples ?? 16} →{" "}
								{job.renderConfig.maxSamples}
							</span>
						</div>
						<div className={j.detailRow}>
							<span className={j.detailK}>max depth</span>
							<span className={j.detailV}>{job.renderConfig.maxDepth}</span>
						</div>
						<div className={j.detailRow}>
							<span className={j.detailK}>integrator</span>
							<span className={j.detailV}>{job.renderConfig.integrator}</span>
						</div>
						{job.renderConfig.isAnimation ? (
							<>
								<div className={j.detailRow}>
									<span className={j.detailK}>frames</span>
									<span className={j.detailV}>
										{job.renderConfig.startFrame} → {job.renderConfig.endFrame}
									</span>
								</div>
								<div className={j.detailRow}>
									<span className={j.detailK}>fps</span>
									<span className={j.detailV}>{job.renderConfig.fps}</span>
								</div>
								<div className={j.detailRow}>
									<span className={j.detailK}>time</span>
									<span className={j.detailV}>
										{job.renderConfig.startTime}s → {job.renderConfig.endTime}s
									</span>
								</div>
							</>
						) : (
							<div className={j.detailRow}>
								<span className={j.detailK}>time</span>
								<span className={j.detailV}>{job.renderConfig.startTime}s</span>
							</div>
						)}
					</div>
				) : null}
			</div>
			<div className={j.rowActions}>
				{job.status === "succeeded" ? (
					<button
						type="button"
						className={`${u.openBtn} ${j.action}`}
						onClick={onDownload}
					>
						<Download size={12} />
						<span>Download</span>
					</button>
				) : null}
				{job.status === "failed" && canRetry ? (
					<button
						type="button"
						className={`${u.openBtn} ${j.action}`}
						onClick={onRetry}
					>
						<RefreshCw size={12} />
						<span>Retry</span>
					</button>
				) : null}
				{job.status === "running" ? (
					<button
						type="button"
						className={`${u.openBtn} ${j.action}`}
						onClick={onCancel}
					>
						<span>Cancel</span>
					</button>
				) : null}
				{job.renderConfig ? (
					<button
						type="button"
						className={j.expand}
						aria-expanded={expanded}
						onClick={onToggleExpand}
					>
						{expanded ? "Hide details" : "Details"}
					</button>
				) : null}
				{isTerminalStatus(job.status) ? (
					<button
						type="button"
						className={j.dismiss}
						aria-label="Dismiss"
						onClick={onRemove}
					>
						<X size={14} />
					</button>
				) : null}
			</div>
		</div>
	);
}

export function JobsModal({
	scene,
	dirHandle,
	onClose,
}: {
	scene: ResolvedScene | null;
	dirHandle: FileSystemDirectoryHandle | null;
	onClose: () => void;
}) {
	const jobs = useJobs();
	const [filter, setFilter] = useState<Filter>("all");
	const [expandedId, setExpandedId] = useState<string | null>(null);

	const counts = useMemo(() => {
		let running = 0;
		let done = 0;
		let failed = 0;
		for (const j of jobs) {
			if (j.status === "succeeded") done++;
			else if (j.status === "failed") failed++;
			else if (isNonTerminalStatus(j.status)) running++;
		}
		return { running, done, failed, all: jobs.length };
	}, [jobs]);

	const filtered = useMemo(() => {
		if (filter === "all") return jobs;
		if (filter === "running")
			return jobs.filter((j) => isNonTerminalStatus(j.status));
		if (filter === "done") return jobs.filter((j) => j.status === "succeeded");
		return jobs.filter((j) => j.status === "failed");
	}, [jobs, filter]);

	const onRemove = useCallback((id: string) => {
		removeJob(id);
	}, []);

	const onDownload = useCallback((id: string) => {
		void downloadCompositePng(id, `composite-${id}.png`);
	}, []);

	const onRetry = useCallback(() => {
		if (!scene || !dirHandle) return;
		void startCloudRender({ scene, dir: dirHandle, enableCache: true });
	}, [scene, dirHandle]);

	const completedCount = counts.done + counts.failed;

	return createPortal(
		// biome-ignore lint/a11y/noStaticElementInteractions: backdrop click-to-close
		// biome-ignore lint/a11y/useKeyWithClickEvents: Escape handled by document listener pattern; backdrop is decorative
		<div
			className={j.overlay}
			onClick={(e) => e.target === e.currentTarget && onClose()}
		>
			<div className={j.modal} role="dialog" aria-label="Cloud renders">
				<div className={j.header}>
					<Cloud size={14} aria-hidden style={{ color: "var(--amber)" }} />
					<span className={j.title}>Cloud renders</span>
					<div className={j.filters}>
						<button
							type="button"
							className={
								filter === "all" ? `${j.chip} ${j.chipActive}` : j.chip
							}
							onClick={() => setFilter("all")}
						>
							All <span className={j.chipN}>{counts.all}</span>
						</button>
						<button
							type="button"
							className={
								filter === "running" ? `${j.chip} ${j.chipActive}` : j.chip
							}
							onClick={() => setFilter("running")}
						>
							Running <span className={j.chipN}>{counts.running}</span>
						</button>
						<button
							type="button"
							className={
								filter === "done" ? `${j.chip} ${j.chipActive}` : j.chip
							}
							onClick={() => setFilter("done")}
						>
							Done <span className={j.chipN}>{counts.done}</span>
						</button>
						<button
							type="button"
							className={
								filter === "failed" ? `${j.chip} ${j.chipActive}` : j.chip
							}
							onClick={() => setFilter("failed")}
						>
							Failed <span className={j.chipN}>{counts.failed}</span>
						</button>
					</div>
					<button
						type="button"
						className={`${u.openBtn} ${j.clearBtn}`}
						disabled={completedCount === 0}
						onClick={clearCompleted}
						title="Remove all completed jobs"
					>
						<Trash2 size={12} />
						<span>Clear completed</span>
					</button>
					<button
						type="button"
						className={j.closeBtn}
						aria-label="Close"
						onClick={onClose}
					>
						<X size={16} />
					</button>
				</div>
				<div className={j.body}>
					{filtered.length === 0 ? (
						<div className={j.empty}>
							<Cloud size={24} aria-hidden />
							<div className={j.emptyTitle}>
								{counts.all === 0
									? "No render jobs yet"
									: "No jobs match this filter"}
							</div>
							<div className={j.emptySub}>
								{counts.all === 0
									? "Click Render to dispatch a scene to the cloud."
									: "Try a different filter to see other jobs."}
							</div>
						</div>
					) : (
						<div className={j.list}>
							{filtered.map((j) => (
								<JobRow
									key={j.id}
									job={j}
									expanded={expandedId === j.id}
									onToggleExpand={() =>
										setExpandedId((cur) => (cur === j.id ? null : j.id))
									}
									onRetry={onRetry}
									onDownload={() => onDownload(j.id)}
									onCancel={() => void userCancelRender(j.id)}
									onRemove={() => onRemove(j.id)}
									canRetry={!!scene && !!dirHandle}
								/>
							))}
						</div>
					)}
				</div>
			</div>
		</div>,
		document.body,
	);
}
