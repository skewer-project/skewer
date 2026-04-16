import * as Collapsible from "@radix-ui/react-collapsible";
import { ChevronRight } from "lucide-react";
import { memo, type ReactNode, useCallback, useEffect, useState } from "react";
import {
	countGraphNodes,
	formatObjectPathKey,
	parseObjectPathKey,
} from "../services/graph-path";
import type {
	Camera,
	Material,
	ResolvedLayer,
	ResolvedScene,
	SceneNode,
	Vec3,
} from "../types/scene";
import { isAnimated } from "../types/scene";
import { AddMaterialDialog } from "./AddMaterialDialog";
import { AddObjectDialog } from "./AddObjectDialog";

function vec3(v: Vec3): string {
	return `[${v.map((n) => +n.toFixed(3)).join(", ")}]`;
}

function displayLabel(node: SceneNode, pathKey: string): string {
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

function kindShort(node: SceneNode): string {
	switch (node.kind) {
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

function ancestorOpenKeys(selectedKey: string | null): string[] {
	if (!selectedKey) return [];
	const p = parseObjectPathKey(selectedKey);
	if (!p || p.indices.length < 2) return [];
	const { tag, layerIdx, indices } = p;
	const keys: string[] = [];
	for (let i = 0; i < indices.length - 1; i++) {
		keys.push(formatObjectPathKey(tag, layerIdx, indices.slice(0, i + 1)));
	}
	return keys;
}

const CameraSection = memo(function CameraSection({
	camera,
}: {
	camera: Camera;
}) {
	return (
		<>
			<div className="inspector-section-head">Camera</div>
			<div className="camera-block">
				<div className="kv-table">
					<div className="kv-row">
						<span className="kv-key">from</span>
						<span className="kv-val">{vec3(camera.look_from)}</span>
					</div>
					<div className="kv-row">
						<span className="kv-key">at</span>
						<span className="kv-val">{vec3(camera.look_at)}</span>
					</div>
					<div className="kv-row">
						<span className="kv-key">vup</span>
						<span className="kv-val">{vec3(camera.vup)}</span>
					</div>
					<div className="kv-row">
						<span className="kv-key">vfov</span>
						<span className="kv-val">{camera.vfov}°</span>
					</div>
					{camera.aperture_radius > 0 && (
						<>
							<div className="kv-row">
								<span className="kv-key">aper</span>
								<span className="kv-val">{camera.aperture_radius}</span>
							</div>
							<div className="kv-row">
								<span className="kv-key">focus</span>
								<span className="kv-val">{camera.focus_distance}</span>
							</div>
						</>
					)}
				</div>
			</div>
		</>
	);
});

const SceneTreeNode = memo(function SceneTreeNode({
	node,
	pathKey,
	depth,
	selectedObjectKey,
	expandedPaths,
	onToggleExpand,
	onSelectObject,
	onAddChild,
}: {
	node: SceneNode;
	pathKey: string;
	depth: number;
	selectedObjectKey: string | null;
	expandedPaths: Set<string>;
	onToggleExpand: (path: string, open: boolean) => void;
	onSelectObject: (key: string | null) => void;
	onAddChild: (parentKey: string) => void;
}) {
	const isGroup = node.kind === "group";
	const isSel = pathKey === selectedObjectKey;
	const pad = depth * 14;

	if (isGroup) {
		const open = expandedPaths.has(pathKey);
		const childCount = node.children.length;
		const childRows: ReactNode[] = [];
		for (let c = 0; c < node.children.length; c++) {
			const ch = node.children[c];
			const childPath = `${pathKey}/${c}`;
			childRows.push(
				<SceneTreeNode
					key={childPath}
					node={ch}
					pathKey={childPath}
					depth={depth + 1}
					selectedObjectKey={selectedObjectKey}
					expandedPaths={expandedPaths}
					onToggleExpand={onToggleExpand}
					onSelectObject={onSelectObject}
					onAddChild={onAddChild}
				/>,
			);
		}
		return (
			<Collapsible.Root
				className="scene-tree-details"
				open={open}
				onOpenChange={(next) => onToggleExpand(pathKey, next)}
			>
				<div className="scene-tree-summary" style={{ paddingLeft: pad }}>
					<Collapsible.Trigger asChild>
						<button
							type="button"
							className="scene-tree-expand"
							aria-label={open ? "Collapse group" : "Expand group"}
						>
							<ChevronRight
								className="scene-tree-expand-icon"
								aria-hidden
								size={18}
								strokeWidth={2}
							/>
						</button>
					</Collapsible.Trigger>
					<button
						type="button"
						className={`data-row data-row-clickable scene-tree-row${isSel ? " data-row-selected" : ""}`}
						onClick={() => onSelectObject(isSel ? null : pathKey)}
					>
						<span className="data-type">{kindShort(node)}</span>{" "}
						<span className="data-name">{displayLabel(node, pathKey)}</span>
						{isAnimated(node.transform) && (
							<span className="data-type" title="animated">
								{" \u23F1"}
							</span>
						)}
						<span className="data-type"> ({childCount})</span>
					</button>
					<button
						type="button"
						className="layer-add-btn scene-tree-add"
						title="Add child"
						onClick={() => onAddChild(pathKey)}
					>
						+
					</button>
				</div>
				<Collapsible.Content className="scene-tree-collapsible-content">
					<div className="scene-tree-children">{childRows}</div>
				</Collapsible.Content>
			</Collapsible.Root>
		);
	}

	return (
		<div className="scene-tree-leaf" style={{ paddingLeft: pad }}>
			<button
				type="button"
				className={`data-row data-row-clickable scene-tree-row${isSel ? " data-row-selected" : ""}`}
				onClick={(e) => {
					e.stopPropagation();
					onSelectObject(isSel ? null : pathKey);
				}}
			>
				<span className="scene-tree-leaf-spacer" aria-hidden="true" />
				<span className="data-type">{kindShort(node)}</span>{" "}
				<span className="data-name">{displayLabel(node, pathKey)}</span>
				{isAnimated(node.transform) && (
					<span className="data-type" title="animated">
						{" \u23F1"}
					</span>
				)}
			</button>
		</div>
	);
});

const LayerCard = memo(function LayerCard({
	layer,
	tag,
	layerIdx,
	selectedObjectKey,
	selectedMaterialKey,
	onSelectObject,
	onSelectMaterial,
	onAddGraphNode,
	onAddMaterial,
	dirHandle,
}: {
	layer: ResolvedLayer;
	tag: "context" | "layer";
	layerIdx: number;
	selectedObjectKey: string | null;
	selectedMaterialKey: string | null;
	onSelectObject: (key: string | null) => void;
	onSelectMaterial: (key: string | null) => void;
	onAddGraphNode: (
		tag: "ctx" | "lyr",
		layerIdx: number,
		parentKey: string,
		node: SceneNode,
	) => void;
	onAddMaterial: (
		tag: "ctx" | "lyr",
		layerIdx: number,
		name: string,
		mat: Material,
	) => void;
	dirHandle: FileSystemDirectoryHandle;
}) {
	const { name, data } = layer;
	const matNames = Object.keys(data.materials);
	const keyPrefix = tag === "context" ? "ctx" : "lyr";
	const rootParentKey = `${keyPrefix}:${layerIdx}`;

	const [showAddObject, setShowAddObject] = useState(false);
	const [addParentKey, setAddParentKey] = useState(rootParentKey);
	const [showAddMaterial, setShowAddMaterial] = useState(false);
	const [expandedPaths, setExpandedPaths] = useState<Set<string>>(
		() => new Set(),
	);

	useEffect(() => {
		const add = ancestorOpenKeys(selectedObjectKey);
		if (add.length === 0) return;
		// eslint-disable-next-line react-hooks/set-state-in-effect
		setExpandedPaths((prev) => {
			const n = new Set(prev);
			for (const k of add) n.add(k);
			return n;
		});
	}, [selectedObjectKey]);

	const nodeCount = countGraphNodes(data.graph);

	const toggleExpand = useCallback((path: string, open: boolean) => {
		setExpandedPaths((prev) => {
			const n = new Set(prev);
			if (open) n.add(path);
			else n.delete(path);
			return n;
		});
	}, []);

	const openAddDialog = useCallback((parentKey: string) => {
		setAddParentKey(parentKey);
		setShowAddObject(true);
	}, []);

	const listTag = keyPrefix as "ctx" | "lyr";

	return (
		<Collapsible.Root defaultOpen className="layer-card">
			<Collapsible.Trigger asChild>
				<button type="button" className="layer-summary">
					<ChevronRight
						className="layer-chevron-icon"
						size={18}
						strokeWidth={2}
						aria-hidden
					/>
					<span
						className={`layer-tag ${tag === "context" ? "layer-tag-ctx" : "layer-tag-lyr"}`}
					>
						{tag === "context" ? "CTX" : "LYR"}
					</span>
					<span className="layer-name">
						{name}
						{data.visible === false ? " ·hidden" : ""}
					</span>
					<span className="layer-counts">
						{matNames.length}m · {nodeCount}n
					</span>
				</button>
			</Collapsible.Trigger>

			<Collapsible.Content className="layer-collapsible-content">
				<div className="layer-body">
					<div className="layer-sub-head layer-sub-head-action">
						<span>Materials</span>
						<button
							type="button"
							className="layer-add-btn"
							title="Add material"
							onClick={(e) => {
								e.stopPropagation();
								setShowAddMaterial(true);
							}}
						>
							+
						</button>
					</div>
					{matNames.map((n) => {
						const key = `${keyPrefix}:${layerIdx}:mat:${n}`;
						const isSelected = key === selectedMaterialKey;
						return (
							<button
								type="button"
								key={n}
								className={`data-row data-row-clickable${isSelected ? " data-row-selected" : ""}`}
								onClick={(e) => {
									e.stopPropagation();
									onSelectMaterial(isSelected ? null : key);
								}}
							>
								<span className="data-name">{n}</span>{" "}
								<span className="data-type">[{data.materials[n].type}]</span>
							</button>
						);
					})}

					<div className="layer-sub-head layer-sub-head-action">
						<span>Graph</span>
						<button
							type="button"
							className="layer-add-btn"
							title="Add root node"
							onClick={(e) => {
								e.stopPropagation();
								openAddDialog(rootParentKey);
							}}
						>
							+
						</button>
					</div>
					{data.graph.map((node, i) => {
						const pathKey = formatObjectPathKey(
							keyPrefix as "ctx" | "lyr",
							layerIdx,
							[i],
						);
						return (
							<SceneTreeNode
								key={pathKey}
								node={node}
								pathKey={pathKey}
								depth={0}
								selectedObjectKey={selectedObjectKey}
								expandedPaths={expandedPaths}
								onToggleExpand={toggleExpand}
								onSelectObject={onSelectObject}
								onAddChild={openAddDialog}
							/>
						);
					})}
				</div>
			</Collapsible.Content>

			{showAddObject && (
				<AddObjectDialog
					parentKey={addParentKey}
					materialNames={matNames}
					dirHandle={dirHandle}
					onAdd={(node) => {
						onAddGraphNode(listTag, layerIdx, addParentKey, node);
						setShowAddObject(false);
					}}
					onCancel={() => setShowAddObject(false)}
				/>
			)}

			{showAddMaterial && (
				<AddMaterialDialog
					existingNames={matNames}
					onAdd={(n, mat) => {
						onAddMaterial(listTag, layerIdx, n, mat);
						setShowAddMaterial(false);
					}}
					onCancel={() => setShowAddMaterial(false)}
				/>
			)}
		</Collapsible.Root>
	);
});

export function SceneInspector({
	scene,
	selectedObjectKey,
	selectedMaterialKey,
	onSelectObject,
	onSelectMaterial,
	onAddGraphNode,
	onAddMaterial,
	dirHandle,
}: {
	scene: ResolvedScene;
	selectedObjectKey: string | null;
	selectedMaterialKey: string | null;
	onSelectObject: (key: string | null) => void;
	onSelectMaterial: (key: string | null) => void;
	onAddGraphNode: (
		tag: "ctx" | "lyr",
		layerIdx: number,
		parentKey: string,
		node: SceneNode,
	) => void;
	onAddMaterial: (
		tag: "ctx" | "lyr",
		layerIdx: number,
		name: string,
		mat: Material,
	) => void;
	dirHandle: FileSystemDirectoryHandle;
}) {
	return (
		<div className="inspector">
			<CameraSection camera={scene.camera} />

			{scene.contexts.length > 0 && (
				<>
					<div className="inspector-section-head">Contexts</div>
					{scene.contexts.map((ctx, i) => (
						<LayerCard
							key={ctx.path}
							layer={ctx}
							tag="context"
							layerIdx={i}
							selectedObjectKey={selectedObjectKey}
							selectedMaterialKey={selectedMaterialKey}
							onSelectObject={onSelectObject}
							onSelectMaterial={onSelectMaterial}
							onAddGraphNode={onAddGraphNode}
							onAddMaterial={onAddMaterial}
							dirHandle={dirHandle}
						/>
					))}
				</>
			)}

			{scene.layers.length > 0 && (
				<>
					<div className="inspector-section-head">Layers</div>
					{scene.layers.map((layer, i) => (
						<LayerCard
							key={layer.path}
							layer={layer}
							tag="layer"
							layerIdx={i}
							selectedObjectKey={selectedObjectKey}
							selectedMaterialKey={selectedMaterialKey}
							onSelectObject={onSelectObject}
							onSelectMaterial={onSelectMaterial}
							onAddGraphNode={onAddGraphNode}
							onAddMaterial={onAddMaterial}
							dirHandle={dirHandle}
						/>
					))}
				</>
			)}
		</div>
	);
}
