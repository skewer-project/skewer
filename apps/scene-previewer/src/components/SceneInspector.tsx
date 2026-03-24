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

function CameraSection({ camera }: { camera: Camera }) {
	return (
		<section>
			<h3>Camera</h3>
			<pre>
				{`from:     ${vec3(camera.look_from)}
at:       ${vec3(camera.look_at)}
vup:      ${vec3(camera.vup)}
vfov:     ${camera.vfov}°${camera.aperture_radius > 0 ? `\naperture: ${camera.aperture_radius}\nfocus:    ${camera.focus_distance}` : ""}`}
			</pre>
		</section>
	);
}

function materialSummary(name: string, mat: Material): string {
	switch (mat.type) {
		case "lambertian":
			return `${name} [lambertian] albedo=${vec3(mat.albedo)}${mat.albedo_texture ? ` tex:${mat.albedo_texture}` : ""}`;
		case "metal":
			return `${name} [metal] albedo=${vec3(mat.albedo)} roughness=${mat.roughness}`;
		case "dielectric":
			return `${name} [dielectric] ior=${mat.ior} roughness=${mat.roughness}`;
	}
}

function objectSummary(obj: SceneObject, i: number): string {
	switch (obj.type) {
		case "sphere":
			return `sphere #${i} center=${vec3(obj.center)} r=${obj.radius} mat="${obj.material}"`;
		case "quad":
			return `quad #${i} mat="${obj.material}"`;
		case "obj":
			return `obj #${i} file="${obj.file}"${obj.material ? ` mat="${obj.material}"` : ""}${obj.auto_fit === false ? " auto_fit=false" : ""}`;
	}
}

function LayerCard({
	layer,
	tag,
}: {
	layer: ResolvedLayer;
	tag: "context" | "layer";
}) {
	const { name, data } = layer;
	const matNames = Object.keys(data.materials);
	const render = data.render;

	return (
		<details open>
			<summary>
				<strong>[{tag}]</strong> {name}
				{" — "}
				{matNames.length} material{matNames.length !== 1 ? "s" : ""},{" "}
				{data.objects.length} object{data.objects.length !== 1 ? "s" : ""}
				{data.visible === false ? " (hidden)" : ""}
			</summary>
			{matNames.length > 0 && (
				<div>
					<h4>Materials</h4>
					<ul>
						{matNames.map((n) => (
							<li key={n}>
								<code>{materialSummary(n, data.materials[n])}</code>
							</li>
						))}
					</ul>
				</div>
			)}
			{data.objects.length > 0 && (
				<div>
					<h4>Objects</h4>
					<ul>
						{data.objects.map((obj, i) => (
							// biome-ignore lint/suspicious/noArrayIndexKey: not a huge deal
							<li key={i}>
								<code>{objectSummary(obj, i)}</code>
							</li>
						))}
					</ul>
				</div>
			)}
			{render && (
				<div>
					<h4>Render</h4>
					<pre>
						{`${render.integrator}  ${render.image.width}×${render.image.height}  samples=${render.max_samples}  depth=${render.max_depth}`}
					</pre>
				</div>
			)}
		</details>
	);
}

export function SceneInspector({ scene }: { scene: ResolvedScene }) {
	const totalObjects = [...scene.contexts, ...scene.layers].reduce(
		(sum, l) => sum + l.data.objects.length,
		0,
	);

	return (
		<div>
			<p>
				{scene.contexts.length} context{scene.contexts.length !== 1 ? "s" : ""},{" "}
				{scene.layers.length} layer{scene.layers.length !== 1 ? "s" : ""},{" "}
				{totalObjects} object{totalObjects !== 1 ? "s" : ""}
				{scene.output_dir ? ` → ${scene.output_dir}` : ""}
			</p>

			<CameraSection camera={scene.camera} />

			{scene.contexts.length > 0 && (
				<section>
					<h3>Contexts</h3>
					{scene.contexts.map((ctx) => (
						<LayerCard key={ctx.path} layer={ctx} tag="context" />
					))}
				</section>
			)}

			<section>
				<h3>Layers</h3>
				{scene.layers.map((layer) => (
					<LayerCard key={layer.path} layer={layer} tag="layer" />
				))}
			</section>
		</div>
	);
}
