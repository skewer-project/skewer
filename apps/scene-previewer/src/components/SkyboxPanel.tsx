import * as Collapsible from "@radix-ui/react-collapsible";
import { ChevronRight, Plus, Trash2 } from "lucide-react";
import { memo, useCallback } from "react";
import u from "../styles/shared/uiPrimitives.module.css";
import type { SkyboxData } from "../types/scene";

const FACES = [
	{ key: "+x" as const, label: "+X (right)" },
	{ key: "-x" as const, label: "−X (left)" },
	{ key: "+y" as const, label: "+Y (top)" },
	{ key: "-y" as const, label: "−Y (bottom)" },
	{ key: "+z" as const, label: "+Z (front)" },
	{ key: "-z" as const, label: "−Z (back)" },
] as const;

interface SkyboxPanelProps {
	skybox: SkyboxData | undefined;
	onSkyboxChange: (sb: SkyboxData | undefined) => void;
}

export const SkyboxPanel = memo(function SkyboxPanel({
	skybox,
	onSkyboxChange,
}: SkyboxPanelProps) {
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

	if (!skybox) {
		return (
			<div className={u.layerCard}>
				<button
					type="button"
					className={u.layerSummary}
					onClick={addSkybox}
					style={{ justifyContent: "flex-start", gap: 8 }}
				>
					<Plus size={16} strokeWidth={2} />
					<span className={u.layerName}>Add Skybox</span>
				</button>
			</div>
		);
	}

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
					<span className={`${u.layerTag} ${u.layerTagCtx}`}>SKYBOX</span>
					<span className={u.layerName}>Settings</span>
					<button
						type="button"
						onClick={(e) => {
							e.stopPropagation();
							removeSkybox();
						}}
						style={{
							marginLeft: "auto",
							background: "none",
							border: "none",
							cursor: "pointer",
							color: "var(--t2)",
						}}
						title="Remove skybox"
					>
						<Trash2 size={14} strokeWidth={2} />
					</button>
				</button>
			</Collapsible.Trigger>

			<Collapsible.Content>
				<div className={u.layerBody}>
					<div className={u.layerSubHead}>Bounds (size)</div>
					<div className={u.kvTable}>
						{(["x", "y", "z"] as const).map((axis) => {
							const idx = axis === "x" ? 0 : axis === "y" ? 1 : 2;
							return (
								<div key={`bounds-${axis}`} className={u.kvRow}>
									<span className={u.kvKey}>
										{axis === "x" ? "X" : axis === "y" ? "Y" : "Z"}
									</span>
									<div className={u.vec3Cell}>
										<input
											type="number"
											className={u.numInput}
											value={skybox.min[idx]}
											step={0.1}
											onChange={(e) =>
												updateBound(axis, "min", Number(e.target.value))
											}
											style={{ width: 72 }}
											title="min"
										/>
										<span
											style={{
												margin: "0 4px",
												color: "var(--t2)",
												fontSize: 12,
											}}
										>
											to
										</span>
										<input
											type="number"
											className={u.numInput}
											value={skybox.max[idx]}
											step={0.1}
											onChange={(e) =>
												updateBound(axis, "max", Number(e.target.value))
											}
											style={{ width: 72 }}
											title="max"
										/>
									</div>
								</div>
							);
						})}
					</div>

					<div className={u.layerSubHead}>Faces</div>
					<div className={u.kvTable}>
						{FACES.map(({ key, label }) => (
							<div key={key} className={u.kvRow}>
								<span className={u.kvKey}>{label}</span>
								<input
									type="text"
									value={skybox.faces[key]}
									placeholder="path/to/texture.jpg"
									onChange={(e) => updateFace(key, e.target.value)}
									style={{
										flex: 1,
										background: "var(--bg1)",
										border: "1px solid var(--border-mid)",
										borderRadius: 4,
										padding: "3px 6px",
										fontSize: 12,
										color: "var(--t1)",
										fontFamily: "var(--mono)",
									}}
								/>
							</div>
						))}
					</div>
				</div>
			</Collapsible.Content>
		</Collapsible.Root>
	);
});
