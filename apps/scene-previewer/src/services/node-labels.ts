import type { SceneNode } from "../types/scene";

export function displayLabel(node: SceneNode, pathKey: string): string {
	if (node.name?.trim()) return node.name.trim();
	const tail = pathKey.split(":").slice(2).join(":") || "0";
	switch (node.kind) {
		case "group":
			return `group ${tail}`;
		case "obj":
			return node.file.split("/").pop() ?? node.file;
		case "sphere":
			return node.material ?? "sphere";
		case "quad":
			return node.material ?? "quad";
	}
}

export function kindShort(node: SceneNode): string {
	return kindShortFromKind(node.kind);
}

export function kindShortFromKind(kind: SceneNode["kind"]): string {
	switch (kind) {
		case "group":
			return "GRP";
		case "sphere":
			return "SPH";
		case "quad":
			return "QUAD";
		case "obj":
			return "OBJ";
	}
}
