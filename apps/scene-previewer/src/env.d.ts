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
