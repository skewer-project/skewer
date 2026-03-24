// File System Access API wrapper.
// Targets Chrome/Edge/Safari only

// TypeScript's bundled DOM lib doesn't include File System Access API types yet.
declare function showDirectoryPicker(options?: {
	mode?: "read" | "readwrite";
}): Promise<FileSystemDirectoryHandle>;

export async function openSceneFolder(): Promise<FileSystemDirectoryHandle> {
	return await showDirectoryPicker({ mode: "read" });
}

// Resolves a relative path (e.g. "layer_room.json" or "../objects/foo.obj")
// against a directory handle by navigating path segments.
async function resolveHandle(
	dir: FileSystemDirectoryHandle,
	relativePath: string,
): Promise<FileSystemFileHandle> {
	const parts = relativePath.split("/");
	let currentDir = dir;

	for (let i = 0; i < parts.length - 1; i++) {
		const part = parts[i];
		if (part === "" || part === ".") continue;
		if (part === "..") {
			// The File System Access API doesn't expose a parent handle.
			// For now, paths going above the scene folder root are not supported.
			throw new Error(
				`Path traversal above scene folder root is not supported: ${relativePath}`,
			);
		}
		currentDir = await currentDir.getDirectoryHandle(part);
	}

	const filename = parts[parts.length - 1];
	return await currentDir.getFileHandle(filename);
}

export async function readTextFile(
	dir: FileSystemDirectoryHandle,
	relativePath: string,
): Promise<string> {
	const fileHandle = await resolveHandle(dir, relativePath);
	const file = await fileHandle.getFile();
	return await file.text();
}

export async function readJsonFile(
	dir: FileSystemDirectoryHandle,
	relativePath: string,
): Promise<unknown> {
	const text = await readTextFile(dir, relativePath);
	return JSON.parse(text);
}
