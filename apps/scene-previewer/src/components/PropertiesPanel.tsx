import type {
	Material,
	ResolvedScene,
	SceneObject,
	Vec3,
} from "../types/scene";

interface Props {
	scene: ResolvedScene;
	objectKey: string;
}

function vec3(v: Vec3): string {
	return `[${v.map((n) => +n.toFixed(3)).join(", ")}]`;
}

/** Parse an objectKey like "ctx:0:3" or "lyr:1:2" into the actual object + metadata. */
function resolveObject(scene: ResolvedScene, key: string) {
	const [tag, layerIdxStr, objIdxStr] = key.split(":");
	const layerIdx = Number(layerIdxStr);
	const objIdx = Number(objIdxStr);

	const list = tag === "ctx" ? scene.contexts : scene.layers;
	const layer = list[layerIdx];
	if (!layer) return null;

	const obj = layer.data.objects[objIdx];
	if (!obj) return null;

	const mat = getMaterialForObject(obj, layer.data.materials);
	return { tag, layerIdx, objIdx, layer, obj, mat };
}

function getMaterialForObject(
	obj: SceneObject,
	materials: Record<string, Material>,
): Material | null {
	const matName = obj.type === "obj" ? obj.material : obj.material;
	if (!matName) return null;
	return materials[matName] ?? null;
}

function ObjectDetails({ obj }: { obj: SceneObject }) {
	switch (obj.type) {
		case "sphere":
			return (
				<div className="kv-table">
					<div className="kv-row">
						<span className="kv-key">center</span>
						<span className="kv-val">{vec3(obj.center)}</span>
					</div>
					<div className="kv-row">
						<span className="kv-key">radius</span>
						<span className="kv-val">{obj.radius}</span>
					</div>
					<div className="kv-row">
						<span className="kv-key">mat</span>
						<span className="kv-val">{obj.material}</span>
					</div>
				</div>
			);
		case "quad":
			return (
				<div className="kv-table">
					{obj.vertices.map((v, i) => (
						// biome-ignore lint/suspicious/noArrayIndexKey: key is stable
						<div key={i} className="kv-row">
							<span className="kv-key">p{i}</span>
							<span className="kv-val">{vec3(v)}</span>
						</div>
					))}
					<div className="kv-row">
						<span className="kv-key">mat</span>
						<span className="kv-val">{obj.material}</span>
					</div>
				</div>
			);
		case "obj": {
			const file = obj.file.split("/").pop() ?? obj.file;
			return (
				<div className="kv-table">
					<div className="kv-row">
						<span className="kv-key">file</span>
						<span className="kv-val">{file}</span>
					</div>
					{obj.material && (
						<div className="kv-row">
							<span className="kv-key">mat</span>
							<span className="kv-val">{obj.material}</span>
						</div>
					)}
					<div className="kv-row">
						<span className="kv-key">fit</span>
						<span className="kv-val">
							{obj.auto_fit !== false ? "yes" : "no"}
						</span>
					</div>
					{obj.transform?.translate && (
						<div className="kv-row">
							<span className="kv-key">pos</span>
							<span className="kv-val">{vec3(obj.transform.translate)}</span>
						</div>
					)}
					{obj.transform?.rotate && (
						<div className="kv-row">
							<span className="kv-key">rot</span>
							<span className="kv-val">{vec3(obj.transform.rotate)}</span>
						</div>
					)}
					{obj.transform?.scale !== undefined && (
						<div className="kv-row">
							<span className="kv-key">scale</span>
							<span className="kv-val">
								{typeof obj.transform.scale === "number"
									? obj.transform.scale
									: vec3(obj.transform.scale)}
							</span>
						</div>
					)}
				</div>
			);
		}
	}
}

function MaterialDetails({ mat }: { mat: Material }) {
	return (
		<div className="kv-table">
			<div className="kv-row">
				<span className="kv-key">type</span>
				<span className="kv-val">{mat.type}</span>
			</div>
			<div className="kv-row">
				<span className="kv-key">albedo</span>
				<span className="kv-val">{vec3(mat.albedo)}</span>
			</div>
			{mat.albedo_texture && (
				<div className="kv-row">
					<span className="kv-key">a.tex</span>
					<span className="kv-val">{mat.albedo_texture.split("/").pop()}</span>
				</div>
			)}
			{mat.type === "metal" && (
				<div className="kv-row">
					<span className="kv-key">rough</span>
					<span className="kv-val">{mat.roughness}</span>
				</div>
			)}
			{mat.type === "dielectric" && (
				<>
					<div className="kv-row">
						<span className="kv-key">ior</span>
						<span className="kv-val">{mat.ior}</span>
					</div>
					<div className="kv-row">
						<span className="kv-key">rough</span>
						<span className="kv-val">{mat.roughness}</span>
					</div>
				</>
			)}
			{mat.emission.some((v) => v > 0) && (
				<div className="kv-row">
					<span className="kv-key">emit</span>
					<span className="kv-val">{vec3(mat.emission)}</span>
				</div>
			)}
		</div>
	);
}

export function PropertiesPanel({ scene, objectKey }: Props) {
	const resolved = resolveObject(scene, objectKey);
	if (!resolved) return null;

	const { tag, obj, layer, mat } = resolved;
	const typeLabel = obj.type.toUpperCase();
	const tagLabel = tag === "ctx" ? "CTX" : "LYR";

	return (
		<div className="properties">
			<div className="properties-header">
				<span
					className={`layer-tag ${tag === "ctx" ? "layer-tag-ctx" : "layer-tag-lyr"}`}
				>
					{tagLabel}
				</span>
				<span className="properties-title">
					{typeLabel} #{resolved.objIdx}
				</span>
				<span className="properties-layer">{layer.name}</span>
			</div>

			<div className="properties-section">
				<div className="inspector-section-head">Geometry</div>
				<div className="properties-body">
					<ObjectDetails obj={obj} />
				</div>
			</div>

			{mat && (
				<div className="properties-section">
					<div className="inspector-section-head">Material</div>
					<div className="properties-body">
						<MaterialDetails mat={mat} />
					</div>
				</div>
			)}
		</div>
	);
}
