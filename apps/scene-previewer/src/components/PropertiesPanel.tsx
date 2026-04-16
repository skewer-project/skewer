import type { RefObject } from "react";
import { resolveNodeAtPath, updateNodeAtPath } from "../services/graph-path";
import { evaluateTransformAt } from "../services/transform";
import type {
	AnimatedTransform,
	Material,
	ObjNode,
	QuadNode,
	ResolvedLayer,
	ResolvedScene,
	SceneNode,
	SphereNode,
	StaticTransform,
	Vec3,
} from "../types/scene";
import { isAnimated } from "../types/scene";
import { MaterialDropdown, NumberField, Toggle, Vec3Field } from "./controls";
import type { ViewportHandle } from "./Viewport";

interface SceneEditorBase {
	scene: ResolvedScene;
	onSceneEdit: (updater: (s: ResolvedScene) => ResolvedScene) => void;
	viewportRef: RefObject<ViewportHandle | null>;
}

interface Props extends SceneEditorBase {
	objectKey: string;
	onDeleteObject: () => void;
	viewportRef: RefObject<ViewportHandle | null>;
}

interface MaterialEditorProps extends SceneEditorBase {
	matKey: string;
}

interface EditorProps extends SceneEditorBase {
	objectKey: string;
	materialNames: string[];
	layer: ResolvedLayer;
	layerTag: string;
	layerIdx: number;
}

function updateMaterial(
	scene: ResolvedScene,
	layerRefKey: string,
	matName: string,
	mutator: (mat: Material) => Material,
): ResolvedScene {
	const parts = layerRefKey.split(":");
	const tag = parts[0];
	const li = Number(parts[1]);
	const listKey = tag === "ctx" ? "contexts" : "layers";

	const newList = [...scene[listKey]];
	const newLayer = { ...newList[li], data: { ...newList[li].data } };
	newLayer.data.materials = { ...newLayer.data.materials };
	newLayer.data.materials[matName] = mutator(newLayer.data.materials[matName]);
	newList[li] = newLayer;

	return { ...scene, [listKey]: newList };
}

function kindLabel(node: SceneNode): string {
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

function leafMaterialName(node: SceneNode): string | null {
	if (node.kind === "group") return null;
	return node.material ?? null;
}

function CommonTransformBlock({
	node,
	objectKey,
	onSceneEdit,
	viewportRef,
	scene,
}: {
	node: SceneNode;
	objectKey: string;
	onSceneEdit: Props["onSceneEdit"];
	viewportRef: Props["viewportRef"];
	scene: ResolvedScene;
}) {
	const animated = isAnimated(node.transform);
	const evaluated = evaluateTransformAt(node.transform, 0);

	if (animated && node.transform) {
		const n = (node.transform as AnimatedTransform).keyframes.length;
		return (
			<div className="kv-table">
				<div className="kv-row">
					<span className="kv-key">motion</span>
					<span className="kv-val">{n} keyframes, rendered at t=0</span>
				</div>
				<div className="kv-row">
					<span className="kv-key">pos</span>
					<span className="kv-val">
						{(evaluated.translate ?? [0, 0, 0])
							.map((x) => +x.toFixed(3))
							.join(", ")}
					</span>
				</div>
				<div className="kv-row">
					<span className="kv-key">rot°</span>
					<span className="kv-val">
						{(evaluated.rotate ?? [0, 0, 0])
							.map((x) => +x.toFixed(2))
							.join(", ")}
					</span>
				</div>
				<div className="kv-row">
					<span className="kv-key">scale</span>
					<span className="kv-val">
						{evaluated.scale === undefined
							? "1"
							: typeof evaluated.scale === "number"
								? String(+evaluated.scale.toFixed(3))
								: evaluated.scale.map((x) => +x.toFixed(3)).join(", ")}
					</span>
				</div>
			</div>
		);
	}

	const pos: Vec3 = evaluated.translate ?? [0, 0, 0];
	const rot: Vec3 = evaluated.rotate ?? [0, 0, 0];
	const scale = typeof evaluated.scale === "number" ? evaluated.scale : 1;
	const vecScale: Vec3 = Array.isArray(evaluated.scale)
		? evaluated.scale
		: [scale, scale, scale];

	function patchTransform(partial: Partial<StaticTransform>) {
		const base = evaluateTransformAt(node.transform, 0);
		const next: StaticTransform = { ...base, ...partial };
		onSceneEdit((s) =>
			updateNodeAtPath(s, objectKey, (o) => ({ ...o, transform: next })),
		);
		viewportRef.current?.applyPatch(scene, objectKey, {
			kind: "node-transform",
			value: next,
		});
	}

	return (
		<div className="kv-table">
			<Vec3Field
				label="pos"
				value={pos}
				onChange={(v) => patchTransform({ translate: v })}
			/>
			<Vec3Field
				label="rot"
				value={rot}
				step={1}
				onChange={(v) => patchTransform({ rotate: v })}
			/>
			{typeof evaluated.scale === "number" || evaluated.scale === undefined ? (
				<NumberField
					label="scale"
					value={scale}
					min={0.001}
					step={0.1}
					onChange={(v) => patchTransform({ scale: v })}
				/>
			) : (
				<Vec3Field
					label="scale"
					value={vecScale}
					min={0.001}
					step={0.1}
					onChange={(v) => patchTransform({ scale: v })}
				/>
			)}
		</div>
	);
}

function SphereEditor({
	obj,
	objectKey,
	onSceneEdit,
	viewportRef,
	scene,
	materialNames,
	layer,
}: EditorProps & { obj: SphereNode }) {
	return (
		<div className="kv-table">
			<Vec3Field
				label="center"
				value={obj.center}
				onChange={(v) => {
					onSceneEdit((s) =>
						updateNodeAtPath(s, objectKey, (o) => ({
							...(o as SphereNode),
							center: v,
						})),
					);
					viewportRef.current?.applyPatch(scene, objectKey, {
						kind: "sphere-center",
						value: v,
					});
				}}
			/>
			<NumberField
				label="radius"
				value={obj.radius}
				min={0.001}
				step={0.1}
				onChange={(v) => {
					onSceneEdit((s) =>
						updateNodeAtPath(s, objectKey, (o) => ({
							...(o as SphereNode),
							radius: v,
						})),
					);
					viewportRef.current?.applyPatch(scene, objectKey, {
						kind: "sphere-radius",
						value: v,
					});
				}}
			/>
			<MaterialDropdown
				label="mat"
				value={obj.material}
				options={materialNames}
				onChange={(name) => {
					onSceneEdit((s) =>
						updateNodeAtPath(s, objectKey, (o) => ({
							...(o as SphereNode),
							material: name,
						})),
					);
					const matData = layer.data.materials[name];
					if (matData) {
						viewportRef.current?.applyPatch(scene, objectKey, {
							kind: "assign-material",
							matData,
						});
					}
				}}
			/>
		</div>
	);
}

function QuadEditor({
	obj,
	objectKey,
	onSceneEdit,
	viewportRef,
	scene,
	materialNames,
	layer,
}: EditorProps & { obj: QuadNode }) {
	const handleVertex = (idx: number, v: Vec3) => {
		const newVerts = [...obj.vertices] as [Vec3, Vec3, Vec3, Vec3];
		newVerts[idx] = v;
		onSceneEdit((s) =>
			updateNodeAtPath(s, objectKey, (o) => ({
				...(o as QuadNode),
				vertices: newVerts,
			})),
		);
		viewportRef.current?.applyPatch(scene, objectKey, {
			kind: "quad-vertices",
			value: newVerts,
		});
	};

	const VERT_LABELS = ["p0", "p1", "p2", "p3"] as const;

	return (
		<div className="kv-table">
			{VERT_LABELS.map((label, i) => (
				<Vec3Field
					key={label}
					label={label}
					value={obj.vertices[i]}
					onChange={(nv) => handleVertex(i, nv)}
				/>
			))}
			<MaterialDropdown
				label="mat"
				value={obj.material}
				options={materialNames}
				onChange={(name) => {
					onSceneEdit((s) =>
						updateNodeAtPath(s, objectKey, (o) => ({
							...(o as QuadNode),
							material: name,
						})),
					);
					const matData = layer.data.materials[name];
					if (matData) {
						viewportRef.current?.applyPatch(scene, objectKey, {
							kind: "assign-material",
							matData,
						});
					}
				}}
			/>
		</div>
	);
}

function ObjEditor({
	obj,
	objectKey,
	onSceneEdit,
	viewportRef,
	scene,
	materialNames,
	layer,
}: EditorProps & { obj: ObjNode }) {
	const file = obj.file.split("/").pop() ?? obj.file;

	return (
		<div className="kv-table">
			<div className="kv-row">
				<span className="kv-key">file</span>
				<span className="kv-val">{file}</span>
			</div>
			{materialNames.length > 0 && (
				<MaterialDropdown
					label="mat"
					value={obj.material ?? materialNames[0]}
					options={materialNames}
					onChange={(name) => {
						onSceneEdit((s) =>
							updateNodeAtPath(s, objectKey, (o) => ({
								...(o as ObjNode),
								material: name,
							})),
						);
						const matData = layer.data.materials[name];
						if (matData) {
							viewportRef.current?.applyPatch(scene, objectKey, {
								kind: "assign-material",
								matData,
							});
						}
					}}
				/>
			)}
			<Toggle
				label="fit"
				value={obj.auto_fit !== false}
				onChange={(v) => {
					onSceneEdit((s) =>
						updateNodeAtPath(s, objectKey, (o) => ({
							...(o as ObjNode),
							auto_fit: v,
						})),
					);
				}}
			/>
		</div>
	);
}

const MATERIAL_TYPES = ["lambertian", "metal", "dielectric"] as const;

function MaterialEditor({
	mat,
	matName,
	layerRefKey,
	layerTag,
	layerIdx,
	onSceneEdit,
	viewportRef,
	scene,
}: SceneEditorBase & {
	mat: Material;
	matName: string;
	layerRefKey: string;
	layerTag: string;
	layerIdx: number;
}) {
	function applyMat(next: Material) {
		onSceneEdit((s) => updateMaterial(s, layerRefKey, matName, () => next));
		viewportRef.current?.applyPatch(scene, layerRefKey, {
			kind: "material",
			matData: next,
			matName,
			layerTag,
			layerIdx,
		});
	}

	function patchMat(partial: Partial<Material>) {
		applyMat({ ...mat, ...partial } as Material);
	}

	function changeType(newType: Material["type"]) {
		if (newType === mat.type) return;
		const roughness = "roughness" in mat ? mat.roughness : 0;
		const ior = "ior" in mat ? mat.ior : 1.5;
		const base = {
			albedo: mat.albedo,
			emission: mat.emission,
			opacity: mat.opacity,
			visible: mat.visible,
			...(mat.albedo_texture ? { albedo_texture: mat.albedo_texture } : {}),
			...(mat.normal_texture ? { normal_texture: mat.normal_texture } : {}),
			...(mat.roughness_texture
				? { roughness_texture: mat.roughness_texture }
				: {}),
		};
		const next: Material =
			newType === "lambertian"
				? { ...base, type: "lambertian" }
				: newType === "metal"
					? { ...base, type: "metal", roughness }
					: { ...base, type: "dielectric", roughness, ior };
		applyMat(next);
	}

	return (
		<div className="kv-table">
			<div className="kv-row">
				<span className="kv-key">type</span>
				<select
					className="mat-select"
					value={mat.type}
					onChange={(e) => changeType(e.target.value as Material["type"])}
				>
					{MATERIAL_TYPES.map((t) => (
						<option key={t} value={t}>
							{t}
						</option>
					))}
				</select>
			</div>

			{mat.type !== "dielectric" && (
				<Vec3Field
					label="albedo"
					value={mat.albedo}
					componentLabels={["r", "g", "b"]}
					min={0}
					max={1}
					step={0.01}
					onChange={(v) => patchMat({ albedo: v })}
				/>
			)}
			{mat.albedo_texture && (
				<div className="kv-row">
					<span className="kv-key">a.tex</span>
					<span className="kv-val">{mat.albedo_texture.split("/").pop()}</span>
				</div>
			)}

			{mat.type === "dielectric" && (
				<NumberField
					label="ior"
					value={mat.ior}
					min={1}
					step={0.01}
					onChange={(v) => patchMat({ ior: v })}
				/>
			)}
			{(mat.type === "metal" || mat.type === "dielectric") && (
				<NumberField
					label="rough"
					value={mat.roughness}
					min={0}
					max={1}
					step={0.01}
					onChange={(v) => patchMat({ roughness: v })}
				/>
			)}

			<Vec3Field
				label="emit"
				value={mat.emission}
				componentLabels={["r", "g", "b"]}
				min={0}
				step={0.1}
				onChange={(v) => patchMat({ emission: v })}
			/>
		</div>
	);
}

export function MaterialPropertiesPanel({
	scene,
	matKey,
	onSceneEdit,
	viewportRef,
}: MaterialEditorProps) {
	const parts = matKey.split(":");
	const tag = parts[0];
	const layerIdx = Number(parts[1]);
	const matName = parts.slice(3).join(":");

	const list = tag === "ctx" ? scene.contexts : scene.layers;
	const layer = list[layerIdx];
	if (!layer) return null;

	const mat = layer.data.materials[matName];
	if (!mat) return null;

	return (
		<div className="properties">
			<div className="properties-header">
				<span className="layer-tag layer-tag-ctx">MAT</span>
				<span className="properties-title">{matName}</span>
				<span className="properties-layer">{layer.name}</span>
			</div>
			<div className="properties-section">
				<div className="inspector-section-head">Material</div>
				<div className="properties-body">
					<MaterialEditor
						mat={mat}
						matName={matName}
						layerRefKey={matKey}
						layerTag={tag}
						layerIdx={layerIdx}
						onSceneEdit={onSceneEdit}
						viewportRef={viewportRef}
						scene={scene}
					/>
				</div>
			</div>
		</div>
	);
}

export function PropertiesPanel({
	scene,
	objectKey,
	onSceneEdit,
	onDeleteObject,
	viewportRef,
}: Props) {
	const resolved = resolveNodeAtPath(scene, objectKey);
	if (!resolved) return null;

	const { tag, layerIdx, layer, node, materialNames } = resolved;
	const layerRefKey = `${tag}:${layerIdx}`;
	const matName = leafMaterialName(node);
	const mat = matName ? (layer.data.materials[matName] ?? null) : null;
	const title =
		node.name?.trim() ||
		`${kindLabel(node)} · ${objectKey.split(":").slice(2).join(":") || "root"}`;

	const editorProps: EditorProps = {
		objectKey,
		onSceneEdit,
		viewportRef,
		scene,
		materialNames,
		layer,
		layerTag: tag,
		layerIdx,
	};

	return (
		<div className="properties">
			<div className="properties-header">
				<span className="layer-tag layer-tag-lyr">{kindLabel(node)}</span>
				<span className="properties-title">{title}</span>
				<span className="properties-layer">{layer.name}</span>
			</div>

			<div className="properties-section">
				<div className="inspector-section-head">Node</div>
				<div className="properties-body">
					<div className="kv-table">
						<div className="kv-row">
							<span className="kv-key">name</span>
							<input
								className="mat-select"
								style={{ flex: 1, minWidth: 0 }}
								value={node.name ?? ""}
								placeholder="(optional)"
								onChange={(e) => {
									const v = e.target.value;
									onSceneEdit((s) =>
										updateNodeAtPath(s, objectKey, (o) => ({
											...o,
											name: v.trim() === "" ? undefined : v,
										})),
									);
								}}
							/>
						</div>
						{isAnimated(node.transform) && (
							<div className="kv-row">
								<span className="kv-key"> </span>
								<span className="kv-val">animated</span>
							</div>
						)}
					</div>
				</div>
			</div>

			<div className="properties-section">
				<div className="inspector-section-head">Transform</div>
				<div className="properties-body">
					<CommonTransformBlock
						node={node}
						objectKey={objectKey}
						onSceneEdit={onSceneEdit}
						viewportRef={viewportRef}
						scene={scene}
					/>
				</div>
			</div>

			{node.kind !== "group" && (
				<div className="properties-section">
					<div className="inspector-section-head">Geometry</div>
					<div className="properties-body">
						{node.kind === "sphere" && (
							<SphereEditor obj={node} {...editorProps} />
						)}
						{node.kind === "quad" && <QuadEditor obj={node} {...editorProps} />}
						{node.kind === "obj" && <ObjEditor obj={node} {...editorProps} />}
						<button
							type="button"
							className="delete-obj-btn"
							onClick={onDeleteObject}
						>
							delete node
						</button>
					</div>
				</div>
			)}

			{node.kind === "group" && (
				<div className="properties-section">
					<div className="properties-body">
						<button
							type="button"
							className="delete-obj-btn"
							onClick={onDeleteObject}
						>
							delete group (subtree)
						</button>
					</div>
				</div>
			)}

			{mat && matName && node.kind !== "group" && (
				<div className="properties-section">
					<div className="inspector-section-head">Material</div>
					<div className="properties-body">
						<MaterialEditor
							mat={mat}
							matName={matName}
							layerRefKey={layerRefKey}
							layerTag={tag}
							layerIdx={layerIdx}
							onSceneEdit={onSceneEdit}
							viewportRef={viewportRef}
							scene={scene}
						/>
					</div>
				</div>
			)}
		</div>
	);
}
