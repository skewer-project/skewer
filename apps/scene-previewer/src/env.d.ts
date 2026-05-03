// Augments the File System Access API types missing from TypeScript's bundled DOM lib.

declare function showDirectoryPicker(options?: {
	mode?: "read" | "readwrite";
}): Promise<FileSystemDirectoryHandle>;

declare interface FileSystemHandle {
	requestPermission(descriptor?: {
		mode?: "read" | "readwrite";
	}): Promise<PermissionState>;
}

declare function showOpenFilePicker(options?: {
	types?: Array<{ description?: string; accept: Record<string, string[]> }>;
	multiple?: boolean;
	excludeAcceptAllOption?: boolean;
}): Promise<FileSystemFileHandle[]>;

// Vite env (set in `.env` — keys optional for typecheck without a local file)
interface ImportMetaEnv {
	readonly VITE_API_URL?: string;
	readonly VITE_FIREBASE_API_KEY?: string;
	readonly VITE_FIREBASE_AUTH_DOMAIN?: string;
	readonly VITE_FIREBASE_PROJECT_ID?: string;
}
interface ImportMeta {
	readonly env: ImportMetaEnv;
}
