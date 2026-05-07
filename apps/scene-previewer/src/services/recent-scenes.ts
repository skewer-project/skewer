// Persists recently opened scene folders using IndexedDB.
// FileSystemDirectoryHandle objects can be stored in IDB but not localStorage.

import { openPreviewerDB, RECENT_SCENES_STORE } from "./previewer-db";

const MAX_ENTRIES = 8;

export interface RecentEntry {
	name: string;
	handle: FileSystemDirectoryHandle;
	lastOpened: number; // epoch ms
}

export async function getRecentScenes(): Promise<RecentEntry[]> {
	try {
		const db = await openPreviewerDB();
		return new Promise((resolve, reject) => {
			const tx = db.transaction(RECENT_SCENES_STORE, "readonly");
			const store = tx.objectStore(RECENT_SCENES_STORE);
			const req = store.getAll();
			req.onsuccess = () => {
				const entries = req.result as RecentEntry[];
				entries.sort((a, b) => b.lastOpened - a.lastOpened);
				resolve(entries.slice(0, MAX_ENTRIES));
			};
			req.onerror = () => reject(req.error);
		});
	} catch {
		return [];
	}
}

export async function addRecentScene(
	name: string,
	handle: FileSystemDirectoryHandle,
): Promise<void> {
	try {
		const db = await openPreviewerDB();
		const tx = db.transaction(RECENT_SCENES_STORE, "readwrite");
		const store = tx.objectStore(RECENT_SCENES_STORE);
		store.put({ name, handle, lastOpened: Date.now() } satisfies RecentEntry);

		// Prune old entries
		const getAll = store.getAll();
		getAll.onsuccess = () => {
			const entries = getAll.result as RecentEntry[];
			if (entries.length > MAX_ENTRIES) {
				entries.sort((a, b) => a.lastOpened - b.lastOpened);
				for (let i = 0; i < entries.length - MAX_ENTRIES; i++) {
					store.delete(entries[i].name);
				}
			}
		};
	} catch {
		// Silently fail — recent scenes is a convenience feature
	}
}

export async function removeRecentScene(name: string): Promise<void> {
	try {
		const db = await openPreviewerDB();
		const tx = db.transaction(RECENT_SCENES_STORE, "readwrite");
		tx.objectStore(RECENT_SCENES_STORE).delete(name);
	} catch {
		// noop
	}
}
