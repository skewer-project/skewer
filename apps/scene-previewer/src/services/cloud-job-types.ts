export type CloudJobClientStatus =
	| "packaging"
	| "uploading-init"
	| "uploading"
	| "submitting";

export type CloudJobServerStatus =
	| "running"
	| "succeeded"
	| "failed"
	| "cancelled";

export type CloudJobStatus = CloudJobClientStatus | CloudJobServerStatus;

export interface CloudJobRenderConfig {
	width: number;
	height: number;
	minSamples?: number;
	maxSamples: number;
	maxDepth: number;
	integrator: string;
	startTime: number;
	endTime: number;
	fps: number;
	startFrame: number;
	endFrame: number;
	isAnimation: boolean;
}

export interface CloudJob {
	id: string;
	createdAt: number;
	completedAt?: number;
	sceneName: string;
	status: CloudJobStatus;
	totalFiles?: number;
	totalBytes?: number;
	uploadedBytes?: number;
	error?: string;
	lastSyncedAt?: number;
	lastSyncError?: string;
	compositeName?: string;
	compositeObjectURL?: string;
	stitchVideoURL?: string;
	abort?: AbortController;
	renderConfig?: CloudJobRenderConfig;
}
