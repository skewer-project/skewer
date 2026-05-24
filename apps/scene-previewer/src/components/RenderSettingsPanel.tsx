import * as Collapsible from "@radix-ui/react-collapsible";
import { ChevronRight, FolderOpen, Plus, Trash2 } from "lucide-react";
import { memo, useCallback } from "react";
import { writeFile } from "../services/fs";
import u from "../styles/shared/uiPrimitives.module.css";
import type { RenderConfig, SkyboxData } from "../types/scene";
import { NumberField, Vec2Field } from "./controls";
import sb from "./SkyboxPanel.module.css";

const FACES = [
	{ key: "+x" as const, label: "+X (right)" },
	{ key: "-x" as const, label: "−X (left)" },
	{ key: "+y" as const, label: "+Y (top)" },
	{ key: "-y" as const, label: "−Y (bottom)" },
	{ key: "+z" as const, label: "+Z (front)" },
	{ key: "-z" as const, label: "−Z (back)" },
] as const;

interface RenderSettingsPanelProps {
	settings: RenderConfig;
	onSettingsChange: (s: RenderConfig) => void;
	startTime: number;
	onStartTimeChange: (n: number) => void;
	endTime: number;
	onEndTimeChange: (n: number) => void;
	fps: number;
	onFpsChange: (n: number) => void;
	skybox: SkyboxData | undefined;
	onSkyboxChange: (s: SkyboxData | undefined) => void;
	dirHandle: FileSystemDirectoryHandle;
}

export const RenderSettingsPanel = memo(function RenderSettingsPanel({
	settings,
	onSettingsChange,
	startTime,
	onStartTimeChange,
	endTime,
	onEndTimeChange,
	fps,
	onFpsChange,
	skybox,
	onSkyboxChange,
	dirHandle,
}: RenderSettingsPanelProps) {
	const updateImage = (v: [number, number]) => {
		onSettingsChange({
			...settings,
			image: { ...settings.image, width: v[0], height: v[1] },
		});
	};

	const patch = (partial: Partial<RenderConfig>) => {
		onSettingsChange({ ...settings, ...partial });
	};

	const updateFace = useCallback(
		(face: keyof SkyboxData["faces"], path: string) => {
			if (!skybox) return;
			onSkyboxChange({
				...skybox,
				faces: { ...skybox.faces, [face]: path },
			});
		},
		[skybox, onSkyboxChange],
	);

	const updateBound = useCallback(
		(axis: "x" | "y" | "z", which: "min" | "max", val: number) => {
			if (!skybox) return;
			const bounds = {
				min: [...skybox.min] as [number, number, number],
				max: [...skybox.max] as [number, number, number],
			};
			const idx = axis === "x" ? 0 : axis === "y" ? 1 : 2;
			if (which === "min") bounds.min[idx] = val;
			else bounds.max[idx] = val;
			onSkyboxChange({ ...skybox, min: bounds.min, max: bounds.max });
		},
		[skybox, onSkyboxChange],
	);

	const addSkybox = useCallback(() => {
		onSkyboxChange({
			min: [-10, -10, -10],
			max: [10, 10, 10],
			faces: { "+x": "", "-x": "", "+y": "", "-y": "", "+z": "", "-z": "" },
		});
	}, [onSkyboxChange]);

	const removeSkybox = useCallback(() => {
		onSkyboxChange(undefined);
	}, [onSkyboxChange]);

	const handleBrowseFace = useCallback(
		async (face: keyof SkyboxData["faces"]) => {
			try {
				const [fileHandle] = await showOpenFilePicker({
					types: [
						{
							description: "Images",
							accept: { "image/*": [".png", ".jpg", ".jpeg"] },
						},
					],
					multiple: false,
				});
				const parts = await dirHandle.resolve(fileHandle);
				if (parts) {
					updateFace(face, parts.join("/"));
					return;
				}
				// File is outside the scene directory — copy it in
				const picked = await fileHandle.getFile();
				const buffer = await picked.arrayBuffer();
				const destPath = `skybox/${picked.name}`;
				await writeFile(dirHandle, destPath, buffer);
				updateFace(face, destPath);
			} catch (err) {
				if (!(err instanceof Error && err.name === "AbortError")) {
					console.warn("[Skybox] browse failed:", err);
				}
			}
		},
		[dirHandle, updateFace],
	);

	return (
		<Collapsible.Root className={u.layerCard} defaultOpen>
			<Collapsible.Trigger asChild>
				<button type="button" className={u.layerSummary}>
					<ChevronRight
						className={u.layerChevronIcon}
						size={18}
						strokeWidth={2}
						aria-hidden
					/>
					<span className={`${u.layerTag} ${u.layerTagCtx}`}>SCENE</span>
					<span className={u.layerName}>Settings</span>
				</button>
			</Collapsible.Trigger>

			<Collapsible.Content>
				<div className={u.layerBody}>
					<div className={u.layerSubHead}>Image</div>
					<div className={u.kvTable}>
						<Vec2Field
							label="res"
							value={[settings.image.width, settings.image.height]}
							onChange={updateImage}
							componentLabels={["w", "h"]}
							min={1}
							integer
						/>
					</div>

					<div className={u.layerSubHead}>Sampling</div>
					<div className={u.kvTable}>
						<Vec2Field
							label="range"
							value={[settings.min_samples ?? 16, settings.max_samples]}
							onChange={(v) => patch({ min_samples: v[0], max_samples: v[1] })}
							componentLabels={["min", "max"]}
							min={1}
							integer
						/>
						<div className={u.kvRow}>
							<span className={u.kvKey}>noise</span>
							<div className={u.vec3Cell}>
								<span className={u.vec3Component}>threshold</span>
								<NumberField
									label=""
									value={settings.noise_threshold ?? 0.01}
									min={0}
									step={0.001}
									onChange={(v) => patch({ noise_threshold: v })}
									inline
								/>
							</div>
						</div>
					</div>

					<div className={u.layerSubHead}>Output</div>
					<div className={u.kvTable}>
						<Vec2Field
							label="time"
							value={[startTime, endTime]}
							onChange={(v) => {
								onStartTimeChange(v[0]);
								onEndTimeChange(v[1]);
							}}
							componentLabels={["start", "end"]}
							min={0}
						/>
						<NumberField
							label="FPS"
							value={fps}
							min={1}
							step={1}
							integer
							onChange={(v) => onFpsChange(Math.round(v))}
						/>
					</div>

					{/* Skybox section */}
					<div className={u.layerSubHead}>Skybox</div>
					{!skybox ? (
						<button type="button" className={sb.addBtn} onClick={addSkybox}>
							<Plus size={14} strokeWidth={2} />
							<span>Add skybox</span>
						</button>
					) : (
						<div className={u.kvTable}>
							{(["x", "y", "z"] as const).map((axis) => {
								const idx = axis === "x" ? 0 : axis === "y" ? 1 : 2;
								return (
									<div key={`bounds-${axis}`} className={sb.boundsRow}>
										<span className={sb.boundsLabel}>{axis.toUpperCase()}</span>
										<div className={sb.boundsCell}>
											<input
												type="number"
												className={sb.boundsNum}
												value={skybox.min[idx]}
												step={0.1}
												onChange={(e) =>
													updateBound(axis, "min", Number(e.target.value))
												}
												title="min"
											/>
											<span className={sb.boundsSep}>to</span>
											<input
												type="number"
												className={sb.boundsNum}
												value={skybox.max[idx]}
												step={0.1}
												onChange={(e) =>
													updateBound(axis, "max", Number(e.target.value))
												}
												title="max"
											/>
										</div>
									</div>
								);
							})}

							<div style={{ marginTop: 8, marginBottom: 2 }}>
								{FACES.map(({ key, label }) => (
									<div key={key} className={sb.faceRow}>
										<span className={sb.faceLabel}>{label}</span>
										<input
											type="text"
											className={sb.faceInput}
											value={skybox.faces[key]}
											placeholder="path/to/texture.jpg"
											onChange={(e) => updateFace(key, e.target.value)}
										/>
										<button
											type="button"
											className={sb.faceBrowseBtn}
											onClick={() => handleBrowseFace(key)}
											title="Browse for image"
										>
											<FolderOpen size={12} strokeWidth={2} />
										</button>
									</div>
								))}
							</div>

							<button
								type="button"
								className={sb.removeBtn}
								onClick={removeSkybox}
								title="Remove skybox"
							>
								<Trash2 size={12} strokeWidth={2} />
								<span>Remove skybox</span>
							</button>
						</div>
					)}

				{/* Transparent background checkbox */}
				<div className={sb.toggleRow}>
					<span className={u.kvKey}>Transparent background</span>
					<label className={sb.checkRow}>
						<input
							type="checkbox"
							checked={settings.transparent_background === true}
							onChange={(e) =>
								patch({ transparent_background: e.target.checked || false })
							}
						/>
					</label>
				</div>
				</div>
			</Collapsible.Content>
		</Collapsible.Root>
	);
});
