import type { RefObject } from "react";
import type {
	Material,
	ObjFileObject,
	QuadObject,
	ResolvedLayer,
	ResolvedScene,
	SceneObject,
	SphereObject,
	Transform,
	Vec3,
} from "../types/scene";
import { MaterialDropdown, NumberField, Toggle, Vec3Field } from "./controls";
import type { ViewportHandle } from "./Viewport";

// ── Props ───────────────────────────────────────────────────

interface Props {
	scene: ResolvedScene;
	objectKey: string;
	onSceneEdit: (updater: (s: ResolvedScene) => ResolvedScene) => void;
	onRebuild: () => void;
	viewportRef: RefObject<ViewportHandle | null>;
}

// ── Scene mutation helpers ──────────────────────────────────

function updateObject(
	scene: ResolvedScene,
	objectKey: string,
	mutator: (obj: SceneObject) => SceneObject,
): ResolvedScene {
	const [tag, liStr, oiStr] = objectKey.split(":");
	const li = Number(liStr);
	const oi = Number(oiStr);
	const listKey = tag === "ctx" ? "contexts" : "layers";

	const newList = [...scene[listKey]];
	const newLayer = { ...newList[li], data: { ...newList[li].data } };
	const newObjects = [...newLayer.data.objects];
	newObjects[oi] = mutator(newObjects[oi]);
	newLayer.data.objects = newObjects;
	newList[li] = newLayer;

	return { ...scene, [listKey]: newList };
}

function updateMaterial(
	scene: ResolvedScene,
	objectKey: string,
	matName: string,
	mutator: (mat: Material) => Material,
): ResolvedScene {
	const [tag, liStr] = objectKey.split(":");
	const li = Number(liStr);
	const listKey = tag === "ctx" ? "contexts" : "layers";

	const newList = [...scene[listKey]];
	const newLayer = { ...newList[li], data: { ...newList[li].data } };
	newLayer.data.materials = { ...newLayer.data.materials };
	newLayer.data.materials[matName] = mutator(newLayer.data.materials[matName]);
	newList[li] = newLayer;

	return { ...scene, [listKey]: newList };
}

/** Parse an objectKey like "ctx:0:3" into the actual object + metadata. */
function resolveObject(scene: ResolvedScene, key: string) {
	const [tag, layerIdxStr, objIdxStr] = key.split(":");
	const layerIdx = Number(layerIdxStr);
	const objIdx = Number(objIdxStr);

	const list = tag === "ctx" ? scene.contexts : scene.layers;
	const layer = list[layerIdx];
	if (!layer) return null;

	const obj = layer.data.objects[objIdx];
	if (!obj) return null;

	const matName = obj.material ?? null;
	const mat = matName ? (layer.data.materials[matName] ?? null) : null;
	const materialNames = Object.keys(layer.data.materials);

	return { tag, layerIdx, objIdx, layer, obj, mat, matName, materialNames };
}

// ── Geometry editors ────────────────────────────────────────

interface EditorProps {
	objectKey: string;
	onSceneEdit: Props["onSceneEdit"];
	onRebuild: Props["onRebuild"];
	viewportRef: Props["viewportRef"];
	materialNames: string[];
	layer: ResolvedLayer;
	layerTag: string;
	layerIdx: number;
}

function SphereEditor({
	obj,
	objectKey,
	onSceneEdit,
	viewportRef,
	materialNames,
	layer,
}: EditorProps & { obj: SphereObject }) {
	return (
		<div className="kv-table">
			<Vec3Field
				label="center"
				value={obj.center}
				onChange={(v) => {
					onSceneEdit((s) =>
						updateObject(s, objectKey, (o) => ({
							...(o as SphereObject),
							center: v,
						})),
					);
					viewportRef.current?.applyPatch(objectKey, {
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
						updateObject(s, objectKey, (o) => ({
							...(o as SphereObject),
							radius: v,
						})),
					);
					viewportRef.current?.applyPatch(objectKey, {
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
						updateObject(s, objectKey, (o) => ({
							...(o as SphereObject),
							material: name,
						})),
					);
					const matData = layer.data.materials[name];
					if (matData) {
						viewportRef.current?.applyPatch(objectKey, {
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
	materialNames,
	layer,
}: EditorProps & { obj: QuadObject }) {
	const handleVertex = (idx: number, v: Vec3) => {
		const newVerts = [...obj.vertices] as [Vec3, Vec3, Vec3, Vec3];
		newVerts[idx] = v;
		onSceneEdit((s) =>
			updateObject(s, objectKey, (o) => ({
				...(o as QuadObject),
				vertices: newVerts,
			})),
		);
		viewportRef.current?.applyPatch(objectKey, {
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
						updateObject(s, objectKey, (o) => ({
							...(o as QuadObject),
							material: name,
						})),
					);
					const matData = layer.data.materials[name];
					if (matData) {
						viewportRef.current?.applyPatch(objectKey, {
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
	onRebuild,
	viewportRef,
	materialNames,
	layer,
}: EditorProps & { obj: ObjFileObject }) {
	const transform = obj.transform ?? {};
	const pos: Vec3 = transform.translate ?? [0, 0, 0];
	const rot: Vec3 = transform.rotate ?? [0, 0, 0];
	const scale = typeof transform.scale === "number" ? transform.scale : 1;
	const file = obj.file.split("/").pop() ?? obj.file;

	function patchTransform(partial: Partial<Transform>) {
		const newTransform: Transform = { ...transform, ...partial };
		onSceneEdit((s) =>
			updateObject(s, objectKey, (o) => ({
				...(o as ObjFileObject),
				transform: newTransform,
			})),
		);
		viewportRef.current?.applyPatch(objectKey, {
			kind: "obj-transform",
			value: newTransform,
		});
	}

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
							updateObject(s, objectKey, (o) => ({
								...(o as ObjFileObject),
								material: name,
							})),
						);
						const matData = layer.data.materials[name];
						if (matData) {
							viewportRef.current?.applyPatch(objectKey, {
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
						updateObject(s, objectKey, (o) => ({
							...(o as ObjFileObject),
							auto_fit: v,
						})),
					);
					onRebuild();
				}}
			/>
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
			<NumberField
				label="scale"
				value={scale}
				min={0.001}
				step={0.1}
				onChange={(v) => patchTransform({ scale: v })}
			/>
		</div>
	);
}

// ── Material editor ─────────────────────────────────────────

const MATERIAL_TYPES = ["lambertian", "metal", "dielectric"] as const;

function MaterialEditor({
	mat,
	matName,
	objectKey,
	layerTag,
	layerIdx,
	onSceneEdit,
	viewportRef,
}: {
	mat: Material;
	matName: string;
	objectKey: string;
	layerTag: string;
	layerIdx: number;
	onSceneEdit: Props["onSceneEdit"];
	viewportRef: Props["viewportRef"];
}) {
	function applyMat(next: Material) {
		onSceneEdit((s) => updateMaterial(s, objectKey, matName, () => next));
		viewportRef.current?.applyPatch(objectKey, {
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

// ── Standalone material panel ────────────────────────────────

export function MaterialPropertiesPanel({
	scene,
	matKey,
	onSceneEdit,
	viewportRef,
}: {
	scene: ResolvedScene;
	matKey: string;
	onSceneEdit: Props["onSceneEdit"];
	viewportRef: Props["viewportRef"];
}) {
	// matKey format: "lyr:0:mat:marble"
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
						objectKey={matKey}
						layerTag={tag}
						layerIdx={layerIdx}
						onSceneEdit={onSceneEdit}
						viewportRef={viewportRef}
					/>
				</div>
			</div>
		</div>
	);
}

// ── Main panel ──────────────────────────────────────────────

export function PropertiesPanel({
	scene,
	objectKey,
	onSceneEdit,
	onRebuild,
	viewportRef,
}: Props) {
	const resolved = resolveObject(scene, objectKey);
	if (!resolved) return null;

	const { tag, obj, layer, mat, matName, materialNames } = resolved;
	const typeLabel = obj.type.toUpperCase();

	const editorProps: EditorProps = {
		objectKey,
		onSceneEdit,
		onRebuild,
		viewportRef,
		materialNames,
		layer,
		layerTag: tag,
		layerIdx: resolved.layerIdx,
	};

	return (
		<div className="properties">
			<div className="properties-header">
				<span className="layer-tag layer-tag-lyr">OBJ</span>
				<span className="properties-title">
					{typeLabel} #{resolved.objIdx}
				</span>
				<span className="properties-layer">{layer.name}</span>
			</div>

			<div className="properties-section">
				<div className="inspector-section-head">Geometry</div>
				<div className="properties-body">
					{obj.type === "sphere" && <SphereEditor obj={obj} {...editorProps} />}
					{obj.type === "quad" && <QuadEditor obj={obj} {...editorProps} />}
					{obj.type === "obj" && <ObjEditor obj={obj} {...editorProps} />}
				</div>
			</div>

			{mat && matName && (
				<div className="properties-section">
					<div className="inspector-section-head">Material</div>
					<div className="properties-body">
						<MaterialEditor
							mat={mat}
							matName={matName}
							objectKey={objectKey}
							layerTag={tag}
							layerIdx={resolved.layerIdx}
							onSceneEdit={onSceneEdit}
							viewportRef={viewportRef}
						/>
					</div>
				</div>
			)}
		</div>
	);
}
