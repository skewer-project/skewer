export type CloudJobStatus =
	| "packaging"
	| "uploading-init"
	| "uploading"
	| "submitting"
	| "running"
	| "succeeded"
	| "failed"
	| "cancelled";

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
	compositeObjectURL?: string;
	smearObjectURL?: string;
	abort?: AbortController;
	renderConfig?: CloudJobRenderConfig;
}
