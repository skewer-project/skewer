import type {
	Camera,
	ResolvedLayer,
	ResolvedScene,
	SceneObject,
	Vec3,
} from "../types/scene";

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

function LayerCard({
	layer,
	tag,
	layerIdx,
	selectedObjectKey,
	selectedMaterialKey,
	onSelectObject,
	onSelectMaterial,
}: {
	layer: ResolvedLayer;
	tag: "context" | "layer";
	// Index into scene.contexts or scene.layers (used to build object/material keys).
	layerIdx: number;
	selectedObjectKey: string | null;
	selectedMaterialKey: string | null;
	onSelectObject: (key: string | null) => void;
	onSelectMaterial: (key: string | null) => void;
}) {
	const { name, data } = layer;
	const matNames = Object.keys(data.materials);
	const keyPrefix = tag === "context" ? "ctx" : "lyr";

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
				{matNames.length > 0 && (
					<>
						<div className="layer-sub-head">Materials</div>
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
					</>
				)}

				{data.objects.length > 0 && (
					<>
						<div className="layer-sub-head">Objects</div>
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
					</>
				)}
			</div>
		</details>
	);
}

export function SceneInspector({
	scene,
	selectedObjectKey,
	selectedMaterialKey,
	onSelectObject,
	onSelectMaterial,
}: {
	scene: ResolvedScene;
	selectedObjectKey: string | null;
	selectedMaterialKey: string | null;
	// Keys are string identifiers like "ctx:0:3" or "lyr:1:mat:metal".
	onSelectObject: (key: string | null) => void;
	onSelectMaterial: (key: string | null) => void;
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
						/>
					))}
				</>
			)}
		</div>
	);
}
