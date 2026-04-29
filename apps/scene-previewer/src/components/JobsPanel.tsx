import { ChevronUp, Download, Loader2, RefreshCw, X } from "lucide-react";
import { useCallback, useState, useSyncExternalStore } from "react";
import type { CloudJob } from "../services/cloud-job-types";
import {
	downloadCompositePng,
	startCloudRender,
	userCancelRender,
} from "../services/cloud-render";
import {
	getSnapshot,
	isNonTerminalStatus,
	removeJob,
	subscribe,
} from "../services/jobs-store";
import type { ResolvedScene } from "../types/scene";

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

function JobCard({
	job,
	onRemove,
	onRetry,
	onDownload,
}: {
	job: CloudJob;
	onRemove: (id: string) => void;
	onRetry: () => void;
	onDownload: (id: string) => void;
}) {
	const isTerminal = !isNonTerminalStatus(job.status);
	const pct =
		job.totalBytes && (job.uploadedBytes ?? 0) > 0
			? Math.min(100, ((job.uploadedBytes ?? 0) / job.totalBytes) * 100)
			: 0;

	if (isTerminal) {
		return (
			<button
				type="button"
				className={`jobs-card jobs-card-terminal`}
				onClick={() => onRemove(job.id)}
			>
				<div className="jobs-card-top">
					<span className="jobs-card-title">{job.sceneName}</span>
					{isNonTerminalStatus(job.status) && job.status === "running" && (
						<button
							type="button"
							className="jobs-card-cancel"
							onClick={(e) => {
								e.stopPropagation();
								void userCancelRender(job.id);
							}}
						>
							<X />
						</button>
					)}
				</div>
				<div className="jobs-card-status-row">
					{job.status === "uploading" && job.totalBytes ? (
						<div
							className="progress-bar-wrap"
							role="progressbar"
							aria-valuenow={Math.round(pct)}
							aria-valuemin={0}
							aria-valuemax={100}
							aria-label="Upload progress"
						>
							<div
								className="progress-bar"
								style={{ width: `${pct.toFixed(1)}%` }}
							/>
						</div>
					) : isNonTerminalStatus(job.status) ? (
						<Loader2 className="jobs-spinner" size={14} />
					) : null}
					<span className="jobs-status-txt">{statusLabel(job)}</span>
				</div>
				{job.error ? (
					<div className="jobs-error" role="alert">
						{job.error}
					</div>
				) : null}
			</button>
		);
	}

	return (
		<li className="jobs-card">
			<div className="jobs-card-top">
				<span className="jobs-card-title">{job.sceneName}</span>
				{isNonTerminalStatus(job.status) && job.status === "running" && (
					<button
						type="button"
						className="jobs-card-cancel"
						onClick={(e) => {
							e.stopPropagation();
							void userCancelRender(job.id);
						}}
					>
						Cancel
					</button>
				)}
				<button
					type="button"
					className="jobs-card-x"
					aria-label="Dismiss"
					onClick={(e) => {
						e.stopPropagation();
						onRemove(job.id);
					}}
				>
					<X size={12} />
				</button>
			</div>
			<div className="jobs-card-status-row">
				{job.status === "uploading" && job.totalBytes ? (
					<div
						className="progress-bar-wrap"
						role="progressbar"
						aria-valuenow={Math.round(pct)}
						aria-valuemin={0}
						aria-valuemax={100}
						aria-label="Upload progress"
					>
						<div
							className="progress-bar"
							style={{ width: `${pct.toFixed(1)}%` }}
						/>
					</div>
				) : isNonTerminalStatus(job.status) ? (
					<Loader2 className="jobs-spinner" size={14} />
				) : null}
				<span className="jobs-status-txt">{statusLabel(job)}</span>
			</div>
			{job.error ? (
				<div className="jobs-error" role="alert">
					{job.error}
				</div>
			) : null}
			{job.status === "succeeded" && job.compositeObjectURL ? (
				<img
					className="jobs-composite"
					src={job.compositeObjectURL}
					alt="Cloud composite render"
				/>
			) : null}
			{job.status === "failed" ? (
				<div className="jobs-actions">
					<button
						type="button"
						className="open-btn jobs-retry"
						onClick={(e) => {
							e.stopPropagation();
							onRetry();
						}}
					>
						<RefreshCw size={12} style={{ marginRight: 4 }} />
						Retry
					</button>
				</div>
			) : null}
			{job.status === "succeeded" ? (
				<div className="jobs-actions">
					<button
						type="button"
						className="open-btn"
						onClick={(e) => {
							e.stopPropagation();
							onDownload(job.id);
						}}
					>
						<Download size={12} style={{ marginRight: 4 }} />
						Download
					</button>
				</div>
			) : null}
		</li>
	);
}

export function JobsPanel({
	scene,
	dirHandle,
}: {
	scene: ResolvedScene;
	dirHandle: FileSystemDirectoryHandle;
}) {
	const jobs = useJobs();
	const [expanded, setExpanded] = useState(false);
	const running = jobs.filter((j) => isNonTerminalStatus(j.status)).length;

	const onRemove = useCallback((id: string) => {
		removeJob(id);
	}, []);

	const onDownload = useCallback((id: string) => {
		void downloadCompositePng(id, `composite-${id}.png`);
	}, []);

	const onRetry = useCallback(() => {
		void startCloudRender({ scene, dir: dirHandle, enableCache: true });
	}, [scene, dirHandle]);

	return (
		<div className="jobs-panel">
			<button
				type="button"
				className="jobs-panel-toggle open-btn"
				onClick={() => setExpanded((e) => !e)}
			>
				{running > 0 ? `Jobs (${running} running)` : "Jobs"}{" "}
				<ChevronUp
					size={12}
					style={{
						marginLeft: 4,
						transform: expanded ? "rotate(0)" : "rotate(180deg)",
					}}
				/>
			</button>
			{expanded && (
				<div className="jobs-panel-inner panel">
					{jobs.length === 0 ? (
						<div className="jobs-empty">No render jobs yet.</div>
					) : (
						<ul className="jobs-list">
							{jobs.map((j) => (
								<JobCard
									key={j.id}
									job={j}
									onRemove={onRemove}
									onRetry={onRetry}
									onDownload={onDownload}
								/>
							))}
						</ul>
					)}
				</div>
			)}
		</div>
	);
}
