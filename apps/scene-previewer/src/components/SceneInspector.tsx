import * as Collapsible from "@radix-ui/react-collapsible";
import { ChevronRight } from "lucide-react";
import { memo, type ReactNode, useCallback, useMemo, useState } from "react";
import {
	countGraphNodes,
	formatObjectPathKey,
	parseObjectPathKey,
} from "../services/graph-path";
import u from "../styles/shared/uiPrimitives.module.css";
import type {
	Camera,
	Material,
	Medium,
	RenderConfig,
	ResolvedLayer,
	ResolvedScene,
	SceneNode,
	Vec3,
} from "../types/scene";
import { isAnimated } from "../types/scene";
import { AddMaterialDialog } from "./AddMaterialDialog";
import { AddMediumDialog } from "./AddMediumDialog";
import { AddObjectDialog } from "./AddObjectDialog";
import { RenderSettingsPanel } from "./RenderSettingsPanel";
import s from "./SceneInspector.module.css";

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
			<div className={u.inspectorSectionHead}>Camera</div>
			<div className={u.cameraBlock}>
				<div className={u.kvTable}>
					<div className={u.kvRow}>
						<span className={u.kvKey}>from</span>
						<span className={u.kvVal}>{vec3(camera.look_from)}</span>
					</div>
					<div className={u.kvRow}>
						<span className={u.kvKey}>at</span>
						<span className={u.kvVal}>{vec3(camera.look_at)}</span>
					</div>
					<div className={u.kvRow}>
						<span className={u.kvKey}>vup</span>
						<span className={u.kvVal}>{vec3(camera.vup)}</span>
					</div>
					<div className={u.kvRow}>
						<span className={u.kvKey}>vfov</span>
						<span className={u.kvVal}>{camera.vfov}°</span>
					</div>
					{camera.aperture_radius > 0 && (
						<>
							<div className={u.kvRow}>
								<span className={u.kvKey}>aper</span>
								<span className={u.kvVal}>{camera.aperture_radius}</span>
							</div>
							<div className={u.kvRow}>
								<span className={u.kvKey}>focus</span>
								<span className={u.kvVal}>{camera.focus_distance}</span>
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
				className={s.sceneTreeDetails}
				open={open}
				onOpenChange={(next) => onToggleExpand(pathKey, next)}
			>
				<div className={s.sceneTreeSummary} style={{ paddingLeft: pad }}>
					<Collapsible.Trigger asChild>
						<button
							type="button"
							className={s.sceneTreeExpand}
							aria-label={open ? "Collapse group" : "Expand group"}
						>
							<ChevronRight
								className={s.sceneTreeExpandIcon}
								aria-hidden
								size={18}
								strokeWidth={2}
							/>
						</button>
					</Collapsible.Trigger>
					<button
						type="button"
						className={`${u.dataRow} ${u.dataRowClickable} ${s.sceneTreeRow}${isSel ? ` ${u.dataRowSelected}` : ""}`}
						onClick={() => onSelectObject(isSel ? null : pathKey)}
					>
						<span className={u.dataType}>{kindShort(node)}</span>{" "}
						<span className={u.dataName}>{displayLabel(node, pathKey)}</span>
						{isAnimated(node.transform) && (
							<span className={u.dataType} title="animated">
								{" \u23F1"}
							</span>
						)}
						<span className={u.dataType}> ({childCount})</span>
					</button>
					<button
						type="button"
						className={`${u.layerAddBtn} ${s.sceneTreeAdd}`}
						title="Add child"
						onClick={() => onAddChild(pathKey)}
					>
						+
					</button>
				</div>
				<Collapsible.Content>
					<div className={s.sceneTreeChildren}>{childRows}</div>
				</Collapsible.Content>
			</Collapsible.Root>
		);
	}

	return (
		<div className={s.sceneTreeLeaf} style={{ paddingLeft: pad }}>
			<button
				type="button"
				className={`${u.dataRow} ${u.dataRowClickable} ${s.sceneTreeRow}${isSel ? ` ${u.dataRowSelected}` : ""}`}
				onClick={(e) => {
					e.stopPropagation();
					onSelectObject(isSel ? null : pathKey);
				}}
			>
				<span className={s.sceneTreeLeafSpacer} aria-hidden="true" />
				<span className={u.dataType}>{kindShort(node)}</span>{" "}
				<span className={u.dataName}>{displayLabel(node, pathKey)}</span>
				{isAnimated(node.transform) && (
					<span className={u.dataType} title="animated">
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
	selectedMediumKey,
	onSelectObject,
	onSelectMaterial,
	onSelectMedium,
	onAddGraphNode,
	onAddMaterial,
	onAddMedium,
	dirHandle,
}: {
	layer: ResolvedLayer;
	tag: "context" | "layer";
	layerIdx: number;
	selectedObjectKey: string | null;
	selectedMaterialKey: string | null;
	selectedMediumKey: string | null;
	onSelectObject: (key: string | null) => void;
	onSelectMaterial: (key: string | null) => void;
	onSelectMedium: (key: string | null) => void;
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
	onAddMedium: (
		tag: "ctx" | "lyr",
		layerIdx: number,
		name: string,
		medium: Medium,
	) => void;
	dirHandle: FileSystemDirectoryHandle;
}) {
	const { name, data } = layer;
	const matNames = Object.keys(data.materials);
	const medNames = Object.keys(data.media ?? {});
	const keyPrefix = tag === "context" ? "ctx" : "lyr";
	const rootParentKey = `${keyPrefix}:${layerIdx}`;

	const [showAddObject, setShowAddObject] = useState(false);
	const [addParentKey, setAddParentKey] = useState(rootParentKey);
	const [showAddMaterial, setShowAddMaterial] = useState(false);
	const [showAddMedium, setShowAddMedium] = useState(false);
	const [expandedPaths, setExpandedPaths] = useState<Set<string>>(
		() => new Set(),
	);

	const autoExpanded = useMemo(
		() => new Set(ancestorOpenKeys(selectedObjectKey)),
		[selectedObjectKey],
	);

	const mergedExpanded = useMemo(() => {
		const merged = new Set(expandedPaths);
		for (const k of autoExpanded) merged.add(k);
		return merged;
	}, [expandedPaths, autoExpanded]);

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
		<Collapsible.Root defaultOpen className={u.layerCard}>
			<Collapsible.Trigger asChild>
				<button type="button" className={u.layerSummary}>
					<ChevronRight
						className={u.layerChevronIcon}
						size={18}
						strokeWidth={2}
						aria-hidden
					/>
					<span
						className={`${u.layerTag} ${tag === "context" ? u.layerTagCtx : u.layerTagLyr}`}
					>
						{tag === "context" ? "CTX" : "LYR"}
					</span>
					<span className={u.layerName}>
						{name}
						{data.visible === false ? " ·hidden" : ""}
					</span>
					<span className={u.layerCounts}>
						{matNames.length}m · {medNames.length}v · {nodeCount}n
					</span>
				</button>
			</Collapsible.Trigger>

			<Collapsible.Content>
				<div className={u.layerBody}>
					<div className={`${u.layerSubHead} ${u.layerSubHeadAction}`}>
						<span>Materials</span>
						<button
							type="button"
							className={u.layerAddBtn}
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
								className={`${u.dataRow} ${u.dataRowClickable}${isSelected ? ` ${u.dataRowSelected}` : ""}`}
								onClick={(e) => {
									e.stopPropagation();
									onSelectMaterial(isSelected ? null : key);
								}}
							>
								<span className={u.dataName}>{n}</span>{" "}
								<span className={u.dataType}>[{data.materials[n].type}]</span>
							</button>
						);
					})}

					<div className="layer-sub-head layer-sub-head-action">
						<span>Media</span>
						<button
							type="button"
							className={u.layerAddBtn}
							title="Add medium"
							onClick={(e) => {
								e.stopPropagation();
								setShowAddMedium(true);
							}}
						>
							+
						</button>
					</div>
					{medNames.map((n) => {
						const key = `${keyPrefix}:${layerIdx}:med:${n}`;
						const isSelected = key === selectedMediumKey;
						const med = data.media?.[n];
						return (
							<button
								type="button"
								key={n}
								className={`data-row data-row-clickable${isSelected ? " data-row-selected" : ""}`}
								onClick={(e) => {
									e.stopPropagation();
									onSelectMedium(isSelected ? null : key);
								}}
							>
								<span className="data-name">{n}</span>{" "}
								<span className="data-type">[{med?.type}]</span>
							</button>
						);
					})}

					<div className={`${u.layerSubHead} ${u.layerSubHeadAction}`}>
						<span>Media</span>
						<button
							type="button"
							className={u.layerAddBtn}
							title="Add medium"
							onClick={(e) => {
								e.stopPropagation();
								setShowAddMedium(true);
							}}
						>
							+
						</button>
					</div>
					{medNames.map((n) => {
						const key = `${keyPrefix}:${layerIdx}:med:${n}`;
						const isSelected = key === selectedMediumKey;
						const med = data.media?.[n];
						return (
							<button
								type="button"
								key={n}
								className={`data-row data-row-clickable${isSelected ? " data-row-selected" : ""}`}
								onClick={(e) => {
									e.stopPropagation();
									onSelectMedium(isSelected ? null : key);
								}}
							>
								<span className="data-name">{n}</span>{" "}
								<span className="data-type">[{med?.type}]</span>
							</button>
						);
					})}

					<div className={`${u.layerSubHead} ${u.layerSubHeadAction}`}>
						<span>Graph</span>
						<button
							type="button"
							className={u.layerAddBtn}
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
								expandedPaths={mergedExpanded}
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

			{showAddMedium && (
				<AddMediumDialog
					existingNames={medNames}
					dirHandle={dirHandle}
					onAdd={(n, med) => {
						onAddMedium(listTag, layerIdx, n, med);
						setShowAddMedium(false);
					}}
					onCancel={() => setShowAddMedium(false)}
				/>
			)}
		</Collapsible.Root>
	);
});

export function SceneInspector({
	scene,
	selectedObjectKey,
	selectedMaterialKey,
	selectedMediumKey,
	onSelectObject,
	onSelectMaterial,
	onSelectMedium,
	onAddGraphNode,
	onAddMaterial,
	onAddMedium,
	dirHandle,
	renderSettings,
	onRenderSettingsChange,
	startTime,
	onStartTimeChange,
	endTime,
	onEndTimeChange,
	fps,
	onFpsChange,
}: {
	scene: ResolvedScene;
	selectedObjectKey: string | null;
	selectedMaterialKey: string | null;
	selectedMediumKey: string | null;
	onSelectObject: (key: string | null) => void;
	onSelectMaterial: (key: string | null) => void;
	onSelectMedium: (key: string | null) => void;
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
	onAddMedium: (
		tag: "ctx" | "lyr",
		layerIdx: number,
		name: string,
		medium: Medium,
	) => void;
	dirHandle: FileSystemDirectoryHandle;
	renderSettings: RenderConfig;
	onRenderSettingsChange: (s: RenderConfig) => void;
	startTime: number;
	onStartTimeChange: (n: number) => void;
	endTime: number;
	onEndTimeChange: (n: number) => void;
	fps: number;
	onFpsChange: (n: number) => void;
}) {
	return (
		<div className={s.inspectRoot}>
			<CameraSection camera={scene.camera} />

			<RenderSettingsPanel
				settings={renderSettings}
				onSettingsChange={onRenderSettingsChange}
				startTime={startTime}
				onStartTimeChange={onStartTimeChange}
				endTime={endTime}
				onEndTimeChange={onEndTimeChange}
				fps={fps}
				onFpsChange={onFpsChange}
			/>

			{scene.contexts.length > 0 && (
				<>
					<div className={u.inspectorSectionHead}>Contexts</div>
					{scene.contexts.map((ctx, i) => (
						<LayerCard
							key={ctx.path}
							layer={ctx}
							tag="context"
							layerIdx={i}
							selectedObjectKey={selectedObjectKey}
							selectedMaterialKey={selectedMaterialKey}
							selectedMediumKey={selectedMediumKey}
							onSelectObject={onSelectObject}
							onSelectMaterial={onSelectMaterial}
							onSelectMedium={onSelectMedium}
							onAddGraphNode={onAddGraphNode}
							onAddMaterial={onAddMaterial}
							onAddMedium={onAddMedium}
							dirHandle={dirHandle}
						/>
					))}
				</>
			)}

			{scene.layers.length > 0 && (
				<>
					<div className={u.inspectorSectionHead}>Layers</div>
					{scene.layers.map((layer, i) => (
						<LayerCard
							key={layer.path}
							layer={layer}
							tag="layer"
							layerIdx={i}
							selectedObjectKey={selectedObjectKey}
							selectedMaterialKey={selectedMaterialKey}
							selectedMediumKey={selectedMediumKey}
							onSelectObject={onSelectObject}
							onSelectMaterial={onSelectMaterial}
							onSelectMedium={onSelectMedium}
							onAddGraphNode={onAddGraphNode}
							onAddMaterial={onAddMaterial}
							onAddMedium={onAddMedium}
							dirHandle={dirHandle}
						/>
					))}
				</>
			)}
		</div>
	);
}
