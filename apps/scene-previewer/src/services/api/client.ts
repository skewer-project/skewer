import { getIdToken } from "../auth";
import { ApiError } from "./types";

function baseUrl(): string {
	const u = import.meta.env.VITE_API_URL?.replace(/\/$/, "");
	if (!u) {
		throw new Error("VITE_API_URL is not set");
	}
	return u;
}

export function apiUrl(path: string): string {
	const p = path.startsWith("/") ? path : `/${path}`;
	return `${baseUrl()}${p}`;
}

async function parseErrorBody(res: Response): Promise<string> {
	try {
		const j = (await res.json()) as { error?: string };
		if (j.error) return j.error;
	} catch {
		// ignore
	}
	return res.statusText || `HTTP ${res.status}`;
}

export async function apiFetch<T>(
	path: string,
	init: RequestInit = {},
	allowRetry = true,
): Promise<T> {
	const doFetch = async (forceTokenRefresh: boolean) => {
		const token = await getIdToken(forceTokenRefresh);
		const headers = new Headers(init.headers);
		headers.set("Authorization", `Bearer ${token}`);
		if (
			init.body &&
			!headers.has("Content-Type") &&
			!headers.has("content-type")
		) {
			headers.set("Content-Type", "application/json");
		}
		return fetch(apiUrl(path), { ...init, headers });
	};

	let res = await doFetch(false);
	if (res.status === 401 && allowRetry) {
		res = await doFetch(true);
	}

	if (res.status === 429) {
		const msg = await parseErrorBody(res);
		throw new ApiError(`Rate limited: ${msg}. Try again in a moment.`, 429);
	}

	if (!res.ok) {
		const msg = await parseErrorBody(res);
		throw new ApiError(msg, res.status);
	}

	if (res.status === 204) {
		return undefined as T;
	}
	const text = await res.text();
	if (!text) {
		return undefined as T;
	}
	return JSON.parse(text) as T;
}
