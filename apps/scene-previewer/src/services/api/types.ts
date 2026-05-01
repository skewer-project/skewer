export interface InitFileEntry {
	path: string;
	content_type?: string;
	size?: number;
}

export interface InitRequest {
	files: InitFileEntry[];
}

export interface InitResponse {
	pipeline_id: string;
	upload_prefix: string;
	upload_urls: Record<string, string>;
	upload_expires_at: string;
}

export interface SubmitRequest {
	scene_path?: string;
	composite_output_uri_prefix?: string;
	enable_cache?: boolean;
	smear_fps?: number;
}

export interface SubmitResponse {
	pipeline_id: string;
	execution_name: string;
	scene_uri: string;
}

export type PipelineStatus =
	| "PIPELINE_STATUS_UNSPECIFIED"
	| "PIPELINE_STATUS_RUNNING"
	| "PIPELINE_STATUS_SUCCEEDED"
	| "PIPELINE_STATUS_FAILED"
	| "PIPELINE_STATUS_CANCELLED";

export interface StatusResponse {
	pipeline_id: string;
	status: PipelineStatus | string;
	error_message?: string;
	layer_outputs?: Record<string, string>;
	composite_output?: string;
}

export class ApiError extends Error {
	readonly status: number;
	constructor(message: string, status: number) {
		super(message);
		this.name = "ApiError";
		this.status = status;
	}
}
