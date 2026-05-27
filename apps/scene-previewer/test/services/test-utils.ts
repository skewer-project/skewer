export type FileMap = Record<string, string | Blob>;

class MemoryFileHandle {
	constructor(
		private readonly path: string,
		private readonly files: Map<string, Blob>,
	) {}

	async getFile(): Promise<File> {
		const blob = this.files.get(this.path);
		if (!blob) {
			throw new DOMException(`File not found: ${this.path}`, "NotFoundError");
		}
		return new File([blob], this.path);
	}

	async createWritable(): Promise<FileSystemWritableFileStream> {
		const chunks: BlobPart[] = [];
		return Object.assign(new WritableStream(), {
			write: async (data: BlobPart) => {
				chunks.push(data);
			},
			close: async () => {
				this.files.set(this.path, new Blob(chunks));
			},
			abort: async () => {},
			seek: async () => {},
			truncate: async () => {},
		});
	}
}

export class MemoryDirectoryHandle {
	readonly kind = "directory";

	constructor(
		readonly name: string,
		private readonly files = new Map<string, Blob>(),
		private readonly prefix = "",
	) {}

	static fromFiles(
		files: FileMap,
		name = "scene-folder",
	): MemoryDirectoryHandle {
		const map = new Map<string, Blob>();
		for (const [path, content] of Object.entries(files)) {
			map.set(
				normalize(path),
				content instanceof Blob ? content : new Blob([content]),
			);
		}
		return new MemoryDirectoryHandle(name, map);
	}

	async getDirectoryHandle(
		name: string,
		options?: FileSystemGetDirectoryOptions,
	): Promise<MemoryDirectoryHandle> {
		const path = normalize(`${this.prefix}${name}`);
		const exists = Array.from(this.files.keys()).some((file) =>
			file.startsWith(`${path}/`),
		);
		if (!exists && !options?.create) {
			throw new DOMException(`Directory not found: ${path}`, "NotFoundError");
		}
		return new MemoryDirectoryHandle(this.name, this.files, `${path}/`);
	}

	async getFileHandle(
		name: string,
		options?: FileSystemGetFileOptions,
	): Promise<MemoryFileHandle> {
		const path = normalize(`${this.prefix}${name}`);
		if (!this.files.has(path) && !options?.create) {
			throw new DOMException(`File not found: ${path}`, "NotFoundError");
		}
		if (!this.files.has(path)) {
			this.files.set(path, new Blob());
		}
		return new MemoryFileHandle(path, this.files);
	}

	async text(path: string): Promise<string> {
		const blob = this.files.get(normalize(path));
		if (!blob) {
			throw new Error(`No file written at ${path}`);
		}
		return await blob.text();
	}
}

export function asDirectoryHandle(
	dir: MemoryDirectoryHandle,
): FileSystemDirectoryHandle {
	return dir as unknown as FileSystemDirectoryHandle;
}

export async function bundleText(
	files: { path: string; blob: Blob }[],
	path: string,
): Promise<string> {
	const file = files.find((entry) => entry.path === path);
	if (!file) {
		throw new Error(`Missing bundle entry: ${path}`);
	}
	return await file.blob.text();
}

function normalize(path: string): string {
	return path
		.split("/")
		.filter((part) => part !== "" && part !== ".")
		.join("/");
}
