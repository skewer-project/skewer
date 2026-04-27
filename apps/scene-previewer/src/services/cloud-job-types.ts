export type CloudJobStatus =
	| "packaging"
	| "uploading-init"
	| "uploading"
	| "submitting"
	| "running"
	| "succeeded"
	| "failed"
	| "cancelled";

export interface CloudJob {
	id: string;
	createdAt: number;
	sceneName: string;
	status: CloudJobStatus;
	totalFiles?: number;
	totalBytes?: number;
	uploadedBytes?: number;
	error?: string;
	compositeObjectURL?: string;
	abort?: AbortController;
}
