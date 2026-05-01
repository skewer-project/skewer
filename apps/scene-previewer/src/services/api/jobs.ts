import { getIdToken } from "../auth";
import { apiFetch, apiUrl } from "./client";
import type {
	InitRequest,
	InitResponse,
	StatusResponse,
	SubmitRequest,
	SubmitResponse,
} from "./types";
import { ApiError } from "./types";

async function errMsg(res: Response): Promise<string> {
	try {
		const j = (await res.json()) as { error?: string };
		if (j.error) {
			return j.error;
		}
	} catch {
		// empty
	}
	return res.statusText || `HTTP ${res.status}`;
}

export async function initJob(req: InitRequest): Promise<InitResponse> {
	return await apiFetch<InitResponse>("/v1/jobs/init", {
		method: "POST",
		body: JSON.stringify(req),
	});
}

export async function submitJob(
	id: string,
	body: SubmitRequest,
): Promise<SubmitResponse> {
	return await apiFetch<SubmitResponse>(
		`/v1/jobs/${encodeURIComponent(id)}/submit`,
		{
			method: "POST",
			body: JSON.stringify(body),
		},
	);
}

/** 409 = submit not done yet; caller should retry the poll. */
export async function getJobStatus(
	id: string,
): Promise<StatusResponse | "pending"> {
	const token = await getIdToken(false);
	const res = await fetch(apiUrl(`/v1/jobs/${encodeURIComponent(id)}`), {
		headers: { Authorization: `Bearer ${token}` },
	});
	if (res.status === 409) {
		return "pending";
	}
	if (res.status === 401) {
		const t2 = await getIdToken(true);
		const res2 = await fetch(apiUrl(`/v1/jobs/${encodeURIComponent(id)}`), {
			headers: { Authorization: `Bearer ${t2}` },
		});
		if (res2.status === 409) {
			return "pending";
		}
		if (!res2.ok) {
			const msg = await errMsg(res2);
			throw new ApiError(msg, res2.status);
		}
		return (await res2.json()) as StatusResponse;
	}
	if (!res.ok) {
		const msg = await errMsg(res);
		throw new ApiError(msg, res.status);
	}
	return (await res.json()) as StatusResponse;
}

export async function cancelJob(id: string): Promise<void> {
	await apiFetch<unknown>(`/v1/jobs/${encodeURIComponent(id)}/cancel`, {
		method: "POST",
	});
}

/**
 * Fetches a composite image through the API (with auth).
 *
 * The API returns a signed GCS URL as JSON rather than issuing a redirect.
 * We then fetch GCS directly so the browser sends the correct Origin header
 * (a cross-origin redirect would cause the browser to send Origin: null, which
 * GCS CORS would reject).
 */
export async function fetchCompositeBlob(
	id: string,
	name: string,
): Promise<Blob> {
	const token = await getIdToken(false);
	const url = apiUrl(
		`/v1/jobs/${encodeURIComponent(id)}/artifacts/composite/${encodeURIComponent(name)}`,
	);
	let res = await fetch(url, {
		headers: { Authorization: `Bearer ${token}` },
	});
	if (res.status === 401) {
		const t2 = await getIdToken(true);
		res = await fetch(url, {
			headers: { Authorization: `Bearer ${t2}` },
		});
	}
	if (!res.ok) {
		throw new Error(
			`Failed to get artifact URL for ${name}: ${res.status} ${res.statusText}`,
		);
	}
	const { url: signedUrl } = (await res.json()) as { url: string };
	const gcsRes = await fetch(signedUrl);
	if (!gcsRes.ok) {
		throw new Error(
			`Failed to load composite ${name}: ${gcsRes.status} ${gcsRes.statusText}`,
		);
	}
	return gcsRes.blob();
}
