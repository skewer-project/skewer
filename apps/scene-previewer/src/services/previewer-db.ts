export const PREVIEWER_DB_NAME = "skewer-previewer";
export const PREVIEWER_DB_VERSION = 2;

export const RECENT_SCENES_STORE = "recent-scenes";
export const CLOUD_JOBS_STORE = "cloud-jobs";

export function openPreviewerDB(): Promise<IDBDatabase> {
	return new Promise((resolve, reject) => {
		const req = indexedDB.open(PREVIEWER_DB_NAME, PREVIEWER_DB_VERSION);
		req.onupgradeneeded = () => {
			const db = req.result;
			if (!db.objectStoreNames.contains(RECENT_SCENES_STORE)) {
				db.createObjectStore(RECENT_SCENES_STORE, { keyPath: "name" });
			}
			if (!db.objectStoreNames.contains(CLOUD_JOBS_STORE)) {
				db.createObjectStore(CLOUD_JOBS_STORE, { keyPath: "id" });
			}
		};
		req.onsuccess = () => resolve(req.result);
		req.onerror = () => reject(req.error);
	});
}
