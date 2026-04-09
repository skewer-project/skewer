// Persists recently opened scene folders using IndexedDB.
// FileSystemDirectoryHandle objects can be stored in IDB but not localStorage.

const DB_NAME = "skewer-previewer";
const STORE_NAME = "recent-scenes";
const DB_VERSION = 1;
const MAX_ENTRIES = 8;

export interface RecentEntry {
	name: string;
	handle: FileSystemDirectoryHandle;
	lastOpened: number; // epoch ms
}

function openDB(): Promise<IDBDatabase> {
	return new Promise((resolve, reject) => {
		const req = indexedDB.open(DB_NAME, DB_VERSION);
		req.onupgradeneeded = () => {
			const db = req.result;
			if (!db.objectStoreNames.contains(STORE_NAME)) {
				db.createObjectStore(STORE_NAME, { keyPath: "name" });
			}
		};
		req.onsuccess = () => resolve(req.result);
		req.onerror = () => reject(req.error);
	});
}

export async function getRecentScenes(): Promise<RecentEntry[]> {
	try {
		const db = await openDB();
		return new Promise((resolve, reject) => {
			const tx = db.transaction(STORE_NAME, "readonly");
			const store = tx.objectStore(STORE_NAME);
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
		const db = await openDB();
		const tx = db.transaction(STORE_NAME, "readwrite");
		const store = tx.objectStore(STORE_NAME);
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
		const db = await openDB();
		const tx = db.transaction(STORE_NAME, "readwrite");
		tx.objectStore(STORE_NAME).delete(name);
	} catch {
		// noop
	}
}
