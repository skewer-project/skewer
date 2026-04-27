import type { RefObject } from "react";
import { useEffect, useMemo, useState } from "react";
import { getFile } from "../services/fs";
import {
	resolveNodeAtPath,
	updateMaterial,
	updateMedium,
	updateNodeAtPath,
} from "../services/graph-path";
import { getNanoVDBBounds } from "../services/nanovdb-parser";
import { evaluateTransformAt } from "../services/transform";
import u from "../styles/shared/uiPrimitives.module.css";
import type {
	AnimatedTransform,
	InterpCurve,
	Keyframe,
	Material,
	Medium,
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
import {
	Dropdown,
	MaterialDropdown,
	NumberField,
	Toggle,
	Vec3Field,
} from "./controls";
import p from "./PropertiesPanel.module.css";
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
	currentTime: number;
	onTimeChange: (t: number) => void;
	dirHandle: FileSystemDirectoryHandle;
}

interface MaterialEditorProps extends SceneEditorBase {
	matKey: string;
}

interface MediumEditorProps extends SceneEditorBase {
	medKey: string;
	dirHandle: FileSystemDirectoryHandle;
}

interface EditorProps extends SceneEditorBase {
	objectKey: string;
	materialNames: string[];
	mediumNames: string[];
	layer: ResolvedLayer;
	layerTag: string;
	layerIdx: number;
	dirHandle: FileSystemDirectoryHandle;
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

function sortKeyframes(kfs: Keyframe[]): Keyframe[] {
	return [...kfs].sort((a, b) => a.time - b.time);
}

type CurvePreset = "linear" | "ease-in" | "ease-out" | "ease-in-out" | "custom";

function curveToPreset(c: InterpCurve | undefined): CurvePreset {
	if (c === undefined || c === "linear") return "linear";
	if (c === "ease-in") return "ease-in";
	if (c === "ease-out") return "ease-out";
	if (c === "ease-in-out") return "ease-in-out";
	return "custom";
}

function presetToCurve(
	p: CurvePreset,
	prev: InterpCurve | undefined,
): InterpCurve {
	if (p === "custom") {
		if (typeof prev === "object" && "bezier" in prev) return prev;
		return { bezier: [0.42, 0, 0.58, 1] };
	}
	return p;
}

function CommonTransformBlock({
	node,
	objectKey,
	onSceneEdit,
	viewportRef,
	scene,
	currentTime,
	onTimeChange,
	layer,
	layerTag,
	layerIdx,
}: {
	node: SceneNode;
	objectKey: string;
	onSceneEdit: Props["onSceneEdit"];
	viewportRef: Props["viewportRef"];
	scene: ResolvedScene;
	currentTime: number;
	onTimeChange: Props["onTimeChange"];
	layer: ResolvedLayer;
	layerTag: string;
	layerIdx: number;
}) {
	const animated = isAnimated(node.transform);
	const [kfSelIdx, setKfSelIdx] = useState(0);

	const sortedKfs = useMemo(() => {
		if (!isAnimated(node.transform)) return [];
		return sortKeyframes(node.transform.keyframes);
	}, [node.transform]);

	const idx = Math.min(kfSelIdx, Math.max(0, sortedKfs.length - 1));
	const selKf = sortedKfs[idx];

	function replaceKeyframes(next: Keyframe[]) {
		onSceneEdit((s) =>
			updateNodeAtPath(s, objectKey, (o) => ({
				...o,
				transform: { keyframes: next },
			})),
		);
	}

	function patchKeyframeRow(rowIndex: number, partial: Partial<Keyframe>) {
		if (!isAnimated(node.transform)) return;
		const next = sortKeyframes(node.transform.keyframes);
		next[rowIndex] = { ...next[rowIndex], ...partial };
		replaceKeyframes(sortKeyframes(next));
	}

	if (animated && node.transform) {
		const anim = node.transform as AnimatedTransform;
		const bezier =
			selKf && typeof selKf.curve === "object" && "bezier" in selKf.curve
				? selKf.curve.bezier
				: ([0.42, 0, 0.58, 1] as [number, number, number, number]); // use "ease-in-out" as default timing curve

		return (
			<div className={u.kvTable}>
				<div className={u.kfList}>
					{sortedKfs.map((kf, rowIdx) => {
						const ev = evaluateTransformAt(anim, kf.time);
						const summary = (ev.translate ?? [0, 0, 0])
							.map((x) => +x.toFixed(2))
							.join(", ");
						const scalePart =
							typeof kf.scale === "number"
								? String(kf.scale)
								: Array.isArray(kf.scale)
									? kf.scale.join("x")
									: "";
						const rowKey = [
							kf.time,
							summary,
							...(kf.translate ?? []),
							...(kf.rotate ?? []),
							scalePart,
							JSON.stringify(kf.curve ?? null),
						].join("|");
						return (
							<button
								key={rowKey}
								type="button"
								className={`${u.kfRow}${rowIdx === idx ? ` ${u.kfRowSelected}` : ""}`}
								onClick={() => {
									onTimeChange(kf.time);
									setKfSelIdx(rowIdx);
								}}
							>
								<span className={u.kfRowTime}>{kf.time.toFixed(2)}s</span>
								<span className={u.kfRowSum}>{summary}</span>
							</button>
						);
					})}
				</div>

				{selKf && idx > 0 && (
					<div className={`${u.kvRow} ${u.kfCurveRow}`}>
						<span className={u.kvKey}>curve</span>
						<select
							className={`${u.matSelect} ${u.kfCurveSelect}`}
							value={curveToPreset(selKf.curve)}
							onChange={(e) => {
								const p = e.target.value as CurvePreset;
								patchKeyframeRow(idx, { curve: presetToCurve(p, selKf.curve) });
							}}
						>
							<option value="linear">linear</option>
							<option value="ease-in">ease-in</option>
							<option value="ease-out">ease-out</option>
							<option value="ease-in-out">ease-in-out</option>
							<option value="custom">custom</option>
						</select>
					</div>
				)}

				{selKf && idx > 0 && curveToPreset(selKf.curve) === "custom" && (
					<div className={u.kfBezierGrid}>
						{(["p1x", "p1y", "p2x", "p2y"] as const).map((label, i) => (
							<NumberField
								key={label}
								label={label}
								value={bezier[i]}
								step={0.01}
								onChange={(v) => {
									const b = [...bezier] as [number, number, number, number];
									b[i] = v;
									patchKeyframeRow(idx, { curve: { bezier: b } });
								}}
							/>
						))}
					</div>
				)}

				{selKf && (
					<>
						<NumberField
							label="time"
							value={selKf.time}
							step={0.05}
							onChange={(v) => {
								patchKeyframeRow(idx, { time: v });
							}}
						/>
						<Vec3Field
							label="pos"
							value={selKf.translate ?? [0, 0, 0]}
							onChange={(v) => patchKeyframeRow(idx, { translate: v })}
						/>
						<Vec3Field
							label="rot"
							value={selKf.rotate ?? [0, 0, 0]}
							step={1}
							onChange={(v) => patchKeyframeRow(idx, { rotate: v })}
						/>
						{typeof selKf.scale === "number" || selKf.scale === undefined ? (
							<NumberField
								label="scale"
								value={typeof selKf.scale === "number" ? selKf.scale : 1}
								min={0.001}
								step={0.1}
								onChange={(v) => patchKeyframeRow(idx, { scale: v })}
							/>
						) : (
							<Vec3Field
								label="scale"
								value={selKf.scale}
								min={0.001}
								step={0.1}
								onChange={(v) => patchKeyframeRow(idx, { scale: v })}
							/>
						)}
					</>
				)}

				<div className={u.kfActions}>
					<button
						type="button"
						className={`${u.deleteObjBtn} ${u.kfActionBtn}`}
						onClick={() => {
							const st = evaluateTransformAt(anim, currentTime);
							const newKf: Keyframe = {
								time: currentTime,
								translate: st.translate,
								rotate: st.rotate,
								scale: st.scale,
								curve: "linear",
							};
							const eps = 1e-4;
							if (
								anim.keyframes.some((k) => Math.abs(k.time - currentTime) < eps)
							) {
								return;
							}
							replaceKeyframes(sortKeyframes([...anim.keyframes, newKf]));
						}}
					>
						add keyframe
					</button>
					<button
						type="button"
						className={`${u.deleteObjBtn} ${u.kfActionBtn}`}
						onClick={() => {
							const next = sortKeyframes(anim.keyframes).filter(
								(_, j) => j !== idx,
							);
							if (next.length === 0) return;
							replaceKeyframes(next);
							setKfSelIdx(0);
						}}
					>
						delete keyframe
					</button>
					<button
						type="button"
						className={`${u.deleteObjBtn} ${u.kfActionBtn}`}
						onClick={() => {
							const st = evaluateTransformAt(anim, currentTime);
							onSceneEdit((s) =>
								updateNodeAtPath(s, objectKey, (o) => ({
									...o,
									transform: st,
								})),
							);
							viewportRef.current?.applyPatch(scene, objectKey, {
								kind: "node-transform",
								value: st,
							});
						}}
					>
						convert to static
					</button>
				</div>
			</div>
		);
	}

	const evaluated = evaluateTransformAt(node.transform, 0);

	const pos: Vec3 = evaluated.translate ?? [0, 0, 0];
	const rot: Vec3 = evaluated.rotate ?? [0, 0, 0];
	const scale = typeof evaluated.scale === "number" ? evaluated.scale : 1;
	const vecScale: Vec3 = Array.isArray(evaluated.scale)
		? evaluated.scale
		: [scale, scale, scale];

	function patchTransform(partial: Partial<StaticTransform>) {
		const base = evaluateTransformAt(node.transform, 0);
		const next: StaticTransform = { ...base, ...partial };
		onSceneEdit((s) => {
			let s2 = updateNodeAtPath(s, objectKey, (o) => ({
				...o,
				transform: next,
			}));

			if (node.kind === "sphere" && node.inside_medium) {
				const med = layer.data.media?.[node.inside_medium];
				if (med && med.type === "nanovdb") {
					if (partial.translate !== undefined) {
						s2 = updateMedium(
							s2,
							`${layerTag}:${layerIdx}`,
							node.inside_medium,
							(m) => ({
								...m,
								translate: partial.translate as Vec3,
							}),
						);
					}
					if (partial.scale !== undefined) {
						const sc =
							typeof partial.scale === "number"
								? partial.scale
								: partial.scale[0];
						s2 = updateMedium(
							s2,
							`${layerTag}:${layerIdx}`,
							node.inside_medium,
							(m) => ({
								...m,
								scale: sc,
							}),
						);
					}
				}
			}
			return s2;
		});
		viewportRef.current?.applyPatch(scene, objectKey, {
			kind: "node-transform",
			value: next,
		});
	}

	return (
		<div className={u.kvTable}>
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
			<button
				type="button"
				className={`${u.deleteObjBtn} ${u.kfActionBtn}`}
				onClick={() => {
					const next: AnimatedTransform = {
						keyframes: [
							{
								time: 0,
								translate: pos,
								rotate: rot,
								scale:
									typeof evaluated.scale === "number" ||
									evaluated.scale === undefined
										? scale
										: vecScale,
								curve: "linear",
							},
						],
					};
					onSceneEdit((s) =>
						updateNodeAtPath(s, objectKey, (o) => ({
							...o,
							transform: next,
						})),
					);
				}}
			>
				animate
			</button>
			<button
				type="button"
				className="delete-obj-btn kf-action-btn"
				onClick={() => {
					const st = evaluateTransformAt(node.transform, currentTime);
					onSceneEdit((s) =>
						updateNodeAtPath(s, objectKey, (o) => ({
							...o,
							transform: st,
						})),
					);
					viewportRef.current?.applyPatch(scene, objectKey, {
						kind: "node-transform",
						value: st,
					});
				}}
			>
				reset to anim
			</button>
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
	mediumNames,
	layer,
	layerTag,
	layerIdx,
	dirHandle,
}: EditorProps & { obj: SphereNode }) {
	const st = evaluateTransformAt(obj.transform, 0);
	const scaleFactor =
		typeof st.scale === "number" ? st.scale : (st.scale?.[0] ?? 1.0);
	const visualRadius = obj.radius * scaleFactor;
	const visualCenter: Vec3 = [
		obj.center[0] + (st.translate?.[0] ?? 0),
		obj.center[1] + (st.translate?.[1] ?? 0),
		obj.center[2] + (st.translate?.[2] ?? 0),
	];

	const handleRadiusChange = async (v: number) => {
		const isNanoVDB =
			obj.inside_medium &&
			layer.data.media?.[obj.inside_medium]?.type === "nanovdb";

		if (isNanoVDB) {
			const newScale = v / obj.radius;
			const nextXform: StaticTransform = { ...st, scale: newScale };
			onSceneEdit((s) =>
				updateNodeAtPath(s, objectKey, (o) => ({
					...o,
					transform: nextXform,
				})),
			);
			viewportRef.current?.applyPatch(scene, objectKey, {
				kind: "node-transform",
				value: nextXform,
			});

			const medName = obj.inside_medium as string;
			onSceneEdit((s) =>
				updateMedium(s, `${layerTag}:${layerIdx}`, medName, (m) => ({
					...m,
					scale: newScale,
				})),
			);
		} else {
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
		}
	};

	const handleCenterChange = (v: Vec3) => {
		const isNanoVDB =
			obj.inside_medium &&
			layer.data.media?.[obj.inside_medium]?.type === "nanovdb";

		if (isNanoVDB) {
			const newTranslate: Vec3 = [
				v[0] - obj.center[0],
				v[1] - obj.center[1],
				v[2] - obj.center[2],
			];
			const nextXform: StaticTransform = { ...st, translate: newTranslate };
			onSceneEdit((s) =>
				updateNodeAtPath(s, objectKey, (o) => ({
					...o,
					transform: nextXform,
				})),
			);
			viewportRef.current?.applyPatch(scene, objectKey, {
				kind: "node-transform",
				value: nextXform,
			});

			const medName = obj.inside_medium as string;
			onSceneEdit((s) =>
				updateMedium(s, `${layerTag}:${layerIdx}`, medName, (m) => ({
					...m,
					translate: v,
				})),
			);
		} else {
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
		}
	};

	return (
		<div className={u.kvTable}>
			<Vec3Field
				label="center"
				value={visualCenter}
				onChange={handleCenterChange}
			/>
			<NumberField
				label="radius"
				value={visualRadius}
				min={0.001}
				step={0.1}
				onChange={handleRadiusChange}
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
							isVolumetric: !!obj.inside_medium,
						});
					}
				}}
			/>
			<Dropdown
				label="inside"
				value={obj.inside_medium ?? ""}
				options={mediumNames}
				noneLabel="vacuum"
				onChange={async (name) => {
					if (name === "") {
						onSceneEdit((s) =>
							updateNodeAtPath(s, objectKey, (o) => ({
								...(o as SphereNode),
								inside_medium: undefined,
							})),
						);
					} else {
						const med = layer.data.media?.[name];
						if (med && med.type === "nanovdb") {
							const bounds = await getNanoVDBBounds(dirHandle, med.file);
							if (bounds) {
								const scale = med.scale ?? 1.0;
								const translate = med.translate ?? [0, 0, 0];
								const trueRadius = bounds.radius;
								const nextXform: StaticTransform = {
									translate,
									rotate: [0, 0, 0],
									scale,
								};

								onSceneEdit((s) =>
									updateNodeAtPath(s, objectKey, (o) => ({
										...(o as SphereNode),
										center: [0, 0, 0],
										radius: trueRadius,
										transform: nextXform,
										inside_medium: name,
									})),
								);

								const vp = viewportRef.current;
								if (vp) {
									vp.applyPatch(scene, objectKey, {
										kind: "sphere-center",
										value: [0, 0, 0],
									});
									vp.applyPatch(scene, objectKey, {
										kind: "sphere-radius",
										value: trueRadius,
									});
									vp.applyPatch(scene, objectKey, {
										kind: "node-transform",
										value: nextXform,
									});
								}
							} else {
								onSceneEdit((s) =>
									updateNodeAtPath(s, objectKey, (o) => ({
										...(o as SphereNode),
										inside_medium: name,
									})),
								);
							}
						} else {
							onSceneEdit((s) =>
								updateNodeAtPath(s, objectKey, (o) => ({
									...(o as SphereNode),
									inside_medium: name,
								})),
							);
						}
					}

					const currentMatName = (obj as SphereNode).material;
					const matData = layer.data.materials[currentMatName] ?? {
						type: "lambertian",
						albedo: [0.8, 0.8, 0.8],
						emission: [0, 0, 0],
						opacity: [1, 1, 1],
						visible: true,
					};

					viewportRef.current?.applyPatch(scene, objectKey, {
						kind: "assign-material",
						matData,
						isVolumetric: name !== "",
					});
				}}
			/>
			<Dropdown
				label="outside"
				value={obj.outside_medium ?? ""}
				options={mediumNames}
				noneLabel="vacuum"
				onChange={(name) => {
					onSceneEdit((s) =>
						updateNodeAtPath(s, objectKey, (o) => ({
							...(o as SphereNode),
							outside_medium: name === "" ? undefined : name,
						})),
					);
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
	mediumNames: _,
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
		<div className={u.kvTable}>
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
	mediumNames: _,
	layer,
}: EditorProps & { obj: ObjNode }) {
	const file = obj.file.split("/").pop() ?? obj.file;

	return (
		<div className={u.kvTable}>
			<div className={u.kvRow}>
				<span className={u.kvKey}>file</span>
				<span className={u.kvVal}>{file}</span>
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
		<div className={u.kvTable}>
			<div className={u.kvRow}>
				<span className={u.kvKey}>type</span>
				<select
					className={u.matSelect}
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
				<div className={u.kvRow}>
					<span className={u.kvKey}>a.tex</span>
					<span className={u.kvVal}>{mat.albedo_texture.split("/").pop()}</span>
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
		<div className={p.root}>
			<div className={p.header}>
				<span className={`${u.layerTag} ${u.layerTagCtx}`}>MAT</span>
				<span className={p.title}>{matName}</span>
				<span className={p.layerLabel}>{layer.name}</span>
			</div>
			<div className={p.section}>
				<div className={`${u.inspectorSectionHead} ${p.headInProperties}`}>
					Material
				</div>
				<div className={p.body}>
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

function MediumEditor({
	med,
	medName,
	layerRefKey,
	onSceneEdit,
	dirHandle,
}: SceneEditorBase & {
	med: Medium;
	medName: string;
	layerRefKey: string;
	dirHandle: FileSystemDirectoryHandle;
}) {
	const [fileStatus, setFileStatus] = useState<"ok" | "missing" | "checking">(
		"checking",
	);

	useEffect(() => {
		let active = true;
		setFileStatus("checking");
		getFile(dirHandle, med.file)
			.then(() => {
				if (active) setFileStatus("ok");
			})
			.catch(() => {
				if (active) setFileStatus("missing");
			});
		return () => {
			active = false;
		};
	}, [med.file, dirHandle]);

	function patchMed(partial: Partial<Medium>) {
		onSceneEdit((s) =>
			updateMedium(
				s,
				layerRefKey,
				medName,
				(m) => ({ ...m, ...partial }) as Medium,
			),
		);
	}

	return (
		<div className="kv-table">
			<div className="kv-row">
				<span className="kv-key">type</span>
				<span className="kv-val">{med.type}</span>
			</div>

			<Vec3Field
				label="sigma_a"
				value={med.sigma_a}
				componentLabels={["r", "g", "b"]}
				min={0}
				step={0.01}
				onChange={(v) => patchMed({ sigma_a: v })}
			/>
			<Vec3Field
				label="sigma_s"
				value={med.sigma_s}
				componentLabels={["r", "g", "b"]}
				min={0}
				step={0.01}
				onChange={(v) => patchMed({ sigma_s: v })}
			/>
			<NumberField
				label="g"
				value={med.g}
				min={-1}
				max={1}
				step={0.05}
				onChange={(v) => patchMed({ g: v })}
			/>
			<NumberField
				label="density"
				value={med.density_multiplier}
				min={0}
				step={0.1}
				onChange={(v) => patchMed({ density_multiplier: v })}
			/>
			<div className="kv-row">
				<span className="kv-key">file</span>
				<div className="kv-val" style={{ display: "flex", gap: "8px" }}>
					<span
						style={{ color: fileStatus === "missing" ? "#ff4444" : "inherit" }}
					>
						{med.file.split("/").pop()}
					</span>
					{fileStatus === "missing" && (
						<span style={{ color: "#ff4444", fontSize: "11px" }}>
							(file missing)
						</span>
					)}
				</div>
			</div>
		</div>
	);
}

export function MediumPropertiesPanel({
	scene,
	medKey,
	onSceneEdit,
	viewportRef,
	dirHandle,
}: MediumEditorProps) {
	const parts = medKey.split(":");
	const tag = parts[0];
	const layerIdx = Number(parts[1]);
	const medName = parts.slice(3).join(":");

	const list = tag === "ctx" ? scene.contexts : scene.layers;
	const layer = list[layerIdx];
	if (!layer) return null;

	const med = layer.data.media?.[medName];
	if (!med) return null;

	return (
		<div className="properties">
			<div className="properties-header">
				<span className="layer-tag layer-tag-med">MED</span>
				<span className="properties-title">{medName}</span>
				<span className="properties-layer">{layer.name}</span>
			</div>
			<div className="properties-section">
				<div className="inspector-section-head">Medium</div>
				<div className="properties-body">
					<MediumEditor
						med={med}
						medName={medName}
						layerRefKey={medKey}
						onSceneEdit={onSceneEdit}
						viewportRef={viewportRef}
						scene={scene}
						dirHandle={dirHandle}
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
	currentTime,
	onTimeChange,
	dirHandle,
}: Props) {
	const resolved = resolveNodeAtPath(scene, objectKey);
	if (!resolved) return null;

	const { tag, layerIdx, layer, node, materialNames, mediumNames } = resolved;
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
		mediumNames,
		layer,
		layerTag: tag,
		layerIdx,
		dirHandle,
	};

	return (
		<div className={p.root}>
			<div className={p.header}>
				<span className={`${u.layerTag} ${u.layerTagLyr}`}>
					{kindLabel(node)}
				</span>
				<span className={p.title}>{title}</span>
				<span className={p.layerLabel}>{layer.name}</span>
			</div>

			<div className={p.section}>
				<div className={`${u.inspectorSectionHead} ${p.headInProperties}`}>
					Node
				</div>
				<div className={p.body}>
					<div className={u.kvTable}>
						<div className={u.kvRow}>
							<span className={u.kvKey}>name</span>
							<input
								className={u.matSelect}
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
							<div className={u.kvRow}>
								<span className={u.kvKey}> </span>
								<span className={u.kvVal}>animated</span>
							</div>
						)}
					</div>
				</div>
			</div>

			<div className={p.section}>
				<div className={`${u.inspectorSectionHead} ${p.headInProperties}`}>
					Transform
				</div>
				<div className={p.body}>
					<CommonTransformBlock
						key={objectKey}
						node={node}
						objectKey={objectKey}
						onSceneEdit={onSceneEdit}
						viewportRef={viewportRef}
						scene={scene}
						currentTime={currentTime}
						onTimeChange={onTimeChange}
						layer={layer}
						layerTag={tag}
						layerIdx={layerIdx}
					/>
				</div>
			</div>

			{node.kind !== "group" && (
				<div className={p.section}>
					<div className={`${u.inspectorSectionHead} ${p.headInProperties}`}>
						Geometry
					</div>
					<div className={p.body}>
						{node.kind === "sphere" && (
							<SphereEditor obj={node} {...editorProps} />
						)}
						{node.kind === "quad" && <QuadEditor obj={node} {...editorProps} />}
						{node.kind === "obj" && <ObjEditor obj={node} {...editorProps} />}
						<button
							type="button"
							className={u.deleteObjBtn}
							onClick={onDeleteObject}
						>
							delete node
						</button>
					</div>
				</div>
			)}

			{node.kind === "group" && (
				<div className={p.section}>
					<div className={p.body}>
						<button
							type="button"
							className={u.deleteObjBtn}
							onClick={onDeleteObject}
						>
							delete group (subtree)
						</button>
					</div>
				</div>
			)}

			{mat && matName && node.kind !== "group" && (
				<div className={p.section}>
					<div className={`${u.inspectorSectionHead} ${p.headInProperties}`}>
						Material
					</div>
					<div className={p.body}>
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

			{node.kind === "sphere" && node.inside_medium && (
				<div className="properties-section">
					<div className="inspector-section-head">Inside Medium</div>
					<div className="properties-body">
						{layer.data.media?.[node.inside_medium] ? (
							<MediumEditor
								med={layer.data.media[node.inside_medium]}
								medName={node.inside_medium}
								layerRefKey={`${layerRefKey}:med:${node.inside_medium}`}
								onSceneEdit={onSceneEdit}
								viewportRef={viewportRef}
								scene={scene}
								dirHandle={dirHandle}
							/>
						) : (
							<div className="kv-row">
								<span className="kv-key">error</span>
								<span className="kv-val">
									Medium "{node.inside_medium}" not found
								</span>
							</div>
						)}
					</div>
				</div>
			)}
		</div>
	);
}
