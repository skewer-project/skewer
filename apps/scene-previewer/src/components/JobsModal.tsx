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
import type { ResolvedScene } from "../types/scene";

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

	const thumb = job.smearObjectURL ? (
		<video
			className="jobs-modal-thumb-img"
			src={job.smearObjectURL}
			muted
			playsInline
			loop
			autoPlay
			aria-label="Animation preview"
		/>
	) : job.compositeObjectURL ? (
		<img className="jobs-modal-thumb-img" src={job.compositeObjectURL} alt="" />
	) : running ? (
		<Loader2 className="jobs-modal-thumb-spin" size={20} />
	) : tone === "err" ? (
		<CloudOff size={20} aria-hidden />
	) : (
		<Cloud size={20} aria-hidden />
	);

	return (
		<div className={`jobs-modal-row jobs-modal-row-${tone}`}>
			<div className="jobs-modal-thumb">{thumb}</div>
			<div className="jobs-modal-row-main">
				<div className="jobs-modal-row-head">
					<span className="jobs-modal-row-title">{job.sceneName}</span>
					<span className={`jobs-modal-row-status jobs-modal-status-${tone}`}>
						{statusLabel(job)}
					</span>
				</div>
				<div className="jobs-modal-row-meta">
					<span className="jobs-modal-row-stamp">{stamp}</span>
					{job.renderConfig ? (
						<>
							<span className="jobs-modal-meta-sep">·</span>
							<span className="jobs-modal-row-settings">
								{settingsLine(job.renderConfig)}
							</span>
						</>
					) : null}
				</div>
				{job.status === "uploading" && job.totalBytes ? (
					<div
						className="jobs-modal-progress"
						role="progressbar"
						aria-label="Upload progress"
					>
						<div
							className="jobs-modal-progress-bar"
							style={{ width: `${pct.toFixed(1)}%` }}
						/>
					</div>
				) : null}
				{job.error ? (
					<div className="jobs-modal-row-error" role="alert">
						{job.error}
					</div>
				) : null}
				{expanded && job.renderConfig ? (
					<div className="jobs-modal-detail">
						<div className="jobs-modal-detail-row">
							<span className="jobs-modal-detail-k">resolution</span>
							<span className="jobs-modal-detail-v">
								{job.renderConfig.width} × {job.renderConfig.height}
							</span>
						</div>
						<div className="jobs-modal-detail-row">
							<span className="jobs-modal-detail-k">samples</span>
							<span className="jobs-modal-detail-v">
								{job.renderConfig.minSamples ?? 16} →{" "}
								{job.renderConfig.maxSamples}
							</span>
						</div>
						<div className="jobs-modal-detail-row">
							<span className="jobs-modal-detail-k">max depth</span>
							<span className="jobs-modal-detail-v">
								{job.renderConfig.maxDepth}
							</span>
						</div>
						<div className="jobs-modal-detail-row">
							<span className="jobs-modal-detail-k">integrator</span>
							<span className="jobs-modal-detail-v">
								{job.renderConfig.integrator}
							</span>
						</div>
						{job.renderConfig.isAnimation ? (
							<>
								<div className="jobs-modal-detail-row">
									<span className="jobs-modal-detail-k">frames</span>
									<span className="jobs-modal-detail-v">
										{job.renderConfig.startFrame} → {job.renderConfig.endFrame}
									</span>
								</div>
								<div className="jobs-modal-detail-row">
									<span className="jobs-modal-detail-k">fps</span>
									<span className="jobs-modal-detail-v">
										{job.renderConfig.fps}
									</span>
								</div>
								<div className="jobs-modal-detail-row">
									<span className="jobs-modal-detail-k">time</span>
									<span className="jobs-modal-detail-v">
										{job.renderConfig.startTime}s → {job.renderConfig.endTime}s
									</span>
								</div>
							</>
						) : (
							<div className="jobs-modal-detail-row">
								<span className="jobs-modal-detail-k">time</span>
								<span className="jobs-modal-detail-v">
									{job.renderConfig.startTime}s
								</span>
							</div>
						)}
					</div>
				) : null}
			</div>
			<div className="jobs-modal-row-actions">
				{job.status === "succeeded" ? (
					<button
						type="button"
						className="open-btn jobs-modal-action"
						onClick={onDownload}
					>
						<Download size={12} />
						<span>Download</span>
					</button>
				) : null}
				{job.status === "failed" && canRetry ? (
					<button
						type="button"
						className="open-btn jobs-modal-action"
						onClick={onRetry}
					>
						<RefreshCw size={12} />
						<span>Retry</span>
					</button>
				) : null}
				{job.status === "running" ? (
					<button
						type="button"
						className="open-btn jobs-modal-action"
						onClick={onCancel}
					>
						<span>Cancel</span>
					</button>
				) : null}
				{job.renderConfig ? (
					<button
						type="button"
						className="jobs-modal-expand"
						aria-expanded={expanded}
						onClick={onToggleExpand}
					>
						{expanded ? "Hide details" : "Details"}
					</button>
				) : null}
				{isTerminalStatus(job.status) ? (
					<button
						type="button"
						className="jobs-modal-dismiss"
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
			className="jobs-modal-overlay"
			onClick={(e) => e.target === e.currentTarget && onClose()}
		>
			<div className="jobs-modal" role="dialog" aria-label="Cloud renders">
				<div className="jobs-modal-header">
					<Cloud size={14} aria-hidden style={{ color: "var(--amber)" }} />
					<span className="jobs-modal-title">Cloud renders</span>
					<div className="jobs-modal-filters">
						<button
							type="button"
							className={`jobs-modal-chip${filter === "all" ? " active" : ""}`}
							onClick={() => setFilter("all")}
						>
							All <span className="jobs-modal-chip-n">{counts.all}</span>
						</button>
						<button
							type="button"
							className={`jobs-modal-chip${filter === "running" ? " active" : ""}`}
							onClick={() => setFilter("running")}
						>
							Running{" "}
							<span className="jobs-modal-chip-n">{counts.running}</span>
						</button>
						<button
							type="button"
							className={`jobs-modal-chip${filter === "done" ? " active" : ""}`}
							onClick={() => setFilter("done")}
						>
							Done <span className="jobs-modal-chip-n">{counts.done}</span>
						</button>
						<button
							type="button"
							className={`jobs-modal-chip${filter === "failed" ? " active" : ""}`}
							onClick={() => setFilter("failed")}
						>
							Failed <span className="jobs-modal-chip-n">{counts.failed}</span>
						</button>
					</div>
					<button
						type="button"
						className="open-btn jobs-modal-clear"
						disabled={completedCount === 0}
						onClick={clearCompleted}
						title="Remove all completed jobs"
					>
						<Trash2 size={12} />
						<span>Clear completed</span>
					</button>
					<button
						type="button"
						className="jobs-modal-close"
						aria-label="Close"
						onClick={onClose}
					>
						<X size={16} />
					</button>
				</div>
				<div className="jobs-modal-body">
					{filtered.length === 0 ? (
						<div className="jobs-modal-empty">
							<Cloud size={24} aria-hidden />
							<div className="jobs-modal-empty-title">
								{counts.all === 0
									? "No render jobs yet"
									: "No jobs match this filter"}
							</div>
							<div className="jobs-modal-empty-sub">
								{counts.all === 0
									? "Click Render to dispatch a scene to the cloud."
									: "Try a different filter to see other jobs."}
							</div>
						</div>
					) : (
						<div className="jobs-modal-list">
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
