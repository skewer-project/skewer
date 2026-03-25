import type {
	Camera,
	Material,
	ResolvedLayer,
	ResolvedScene,
	SceneObject,
	Vec3,
} from "../types/scene";

function vec3(v: Vec3): string {
	return `[${v.map((n) => +n.toFixed(3)).join(", ")}]`;
}

function materialValueStr(mat: Material): string {
	switch (mat.type) {
		case "lambertian":
			if (mat.albedo_texture) {
				const file = mat.albedo_texture.split("/").pop() ?? mat.albedo_texture;
				return `tex:${file}`;
			}
			return vec3(mat.albedo);
		case "metal":
			return `${vec3(mat.albedo)}  r=${mat.roughness}`;
		case "dielectric":
			return `ior=${mat.ior}  r=${mat.roughness}`;
	}
}

function objectValueStr(obj: SceneObject): string {
	switch (obj.type) {
		case "sphere":
			return `${vec3(obj.center)}  r=${obj.radius}  "${obj.material}"`;
		case "quad":
			return `"${obj.material}"`;
		case "obj": {
			const file = obj.file.split("/").pop() ?? obj.file;
			return `${file}${obj.material ? `  "${obj.material}"` : ""}`;
		}
	}
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
	onSelectObject,
}: {
	layer: ResolvedLayer;
	tag: "context" | "layer";
	layerIdx: number;
	selectedObjectKey: string | null;
	onSelectObject: (key: string | null) => void;
}) {
	const { name, data } = layer;
	const matNames = Object.keys(data.materials);
	const render = data.render;
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
						{matNames.map((n) => (
							<div key={n} className="data-row">
								<span className="data-name">{n}</span>{" "}
								<span className="data-type">[{data.materials[n].type}]</span>{" "}
								{materialValueStr(data.materials[n])}
							</div>
						))}
					</>
				)}

				{data.objects.length > 0 && (
					<>
						<div className="layer-sub-head">Objects</div>
						{data.objects.map((obj, i) => {
							const key = `${keyPrefix}:${layerIdx}:${i}`;
							const isSelected = key === selectedObjectKey;
							return (
								<div
									role="button"
									key={i}
									className={`data-row data-row-clickable${isSelected ? " data-row-selected" : ""}`}
									onClick={(e) => {
										e.stopPropagation();
										onSelectObject(isSelected ? null : key);
									}}
								>
									<span className="data-type">
										#{i} {obj.type}
									</span>{" "}
									{objectValueStr(obj)}
								</div>
							);
						})}
					</>
				)}

				{render && (
					<>
						<div className="layer-sub-head">Render</div>
						<div className="render-row">
							<span className="render-kv">
								<span className="render-k">int </span>
								{render.integrator}
							</span>
							<span className="render-kv">
								<span className="render-k">res </span>
								{render.image.width}×{render.image.height}
							</span>
							<span className="render-kv">
								<span className="render-k">spp </span>
								{render.max_samples}
							</span>
							<span className="render-kv">
								<span className="render-k">d </span>
								{render.max_depth}
							</span>
						</div>
					</>
				)}
			</div>
		</details>
	);
}

export function SceneInspector({
	scene,
	selectedObjectKey,
	onSelectObject,
}: {
	scene: ResolvedScene;
	selectedObjectKey: string | null;
	onSelectObject: (key: string | null) => void;
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
							onSelectObject={onSelectObject}
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
							onSelectObject={onSelectObject}
						/>
					))}
				</>
			)}
		</div>
	);
}
