import { memo, useState } from "react";
import type {
	Camera,
	Material,
	ResolvedLayer,
	ResolvedScene,
	SceneObject,
	Vec3,
} from "../types/scene";
import { AddMaterialDialog } from "./AddMaterialDialog";
import { AddObjectDialog } from "./AddObjectDialog";

function vec3(v: Vec3): string {
	return `[${v.map((n) => +n.toFixed(3)).join(", ")}]`;
}

function objectLabel(obj: SceneObject): string {
	if (obj.type === "obj") {
		return obj.file.split("/").pop() ?? obj.file;
	}
	return obj.material ?? "";
}

function CameraSection({ camera }: { camera: Camera }) {
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
}

const LayerCard = memo(function LayerCard({
	layer,
	tag,
	layerIdx,
	selectedObjectKey,
	selectedMaterialKey,
	onSelectObject,
	onSelectMaterial,
	onAddObject,
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
	onAddObject: (tag: "ctx" | "lyr", layerIdx: number, obj: SceneObject) => void;
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

	const [showAddObject, setShowAddObject] = useState(false);
	const [showAddMaterial, setShowAddMaterial] = useState(false);

	return (
		<details className="layer-card">
			<summary className="layer-summary">
				<svg
					className="layer-chevron"
					viewBox="0 0 9 9"
					fill="none"
					aria-hidden="true"
				>
					<path
						d="M2.5 1.5 6 4.5l-3.5 3"
						stroke="currentColor"
						strokeWidth="1.4"
						strokeLinecap="round"
						strokeLinejoin="round"
					/>
				</svg>
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
					{matNames.length}m · {data.objects.length}o
				</span>
			</summary>

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
					<span>Objects</span>
					<button
						type="button"
						className="layer-add-btn"
						title="Add object"
						onClick={(e) => {
							e.stopPropagation();
							setShowAddObject(true);
						}}
					>
						+
					</button>
				</div>
				{data.objects.map((obj, i) => {
					const key = `${keyPrefix}:${layerIdx}:${i}`;
					const isSelected = key === selectedObjectKey;
					const label = objectLabel(obj);
					return (
						<button
							type="button"
							key={key}
							className={`data-row data-row-clickable${isSelected ? " data-row-selected" : ""}`}
							onClick={(e) => {
								e.stopPropagation();
								onSelectObject(isSelected ? null : key);
							}}
						>
							<span className="data-type">#{i}</span>{" "}
							<span className="data-name">{obj.type}</span>
							{label && <span className="data-type"> {label}</span>}
						</button>
					);
				})}
			</div>

			{showAddObject && (
				<AddObjectDialog
					materialNames={matNames}
					dirHandle={dirHandle}
					onAdd={(obj) => {
						onAddObject(keyPrefix as "ctx" | "lyr", layerIdx, obj);
						setShowAddObject(false);
					}}
					onCancel={() => setShowAddObject(false)}
				/>
			)}

			{showAddMaterial && (
				<AddMaterialDialog
					existingNames={matNames}
					onAdd={(name, mat) => {
						onAddMaterial(keyPrefix as "ctx" | "lyr", layerIdx, name, mat);
						setShowAddMaterial(false);
					}}
					onCancel={() => setShowAddMaterial(false)}
				/>
			)}
		</details>
	);
});

export function SceneInspector({
	scene,
	selectedObjectKey,
	selectedMaterialKey,
	onSelectObject,
	onSelectMaterial,
	onAddObject,
	onAddMaterial,
	dirHandle,
}: {
	scene: ResolvedScene;
	selectedObjectKey: string | null;
	selectedMaterialKey: string | null;
	onSelectObject: (key: string | null) => void;
	onSelectMaterial: (key: string | null) => void;
	onAddObject: (tag: "ctx" | "lyr", layerIdx: number, obj: SceneObject) => void;
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
							onAddObject={onAddObject}
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
							onAddObject={onAddObject}
							onAddMaterial={onAddMaterial}
							dirHandle={dirHandle}
						/>
					))}
				</>
			)}
		</div>
	);
}
