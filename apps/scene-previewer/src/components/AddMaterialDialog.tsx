import { useState } from "react";
import { createPortal } from "react-dom";
import d from "../styles/shared/dialogs.module.css";
import u from "../styles/shared/uiPrimitives.module.css";
import type { Material } from "../types/scene";

const MATERIAL_TYPES = ["lambertian", "metal", "dielectric"] as const;

function defaultMaterial(type: Material["type"]): Material {
	const base = {
		albedo: [0.5, 0.5, 0.5] as [number, number, number],
		emission: [0, 0, 0] as [number, number, number],
		opacity: [1, 1, 1] as [number, number, number],
		visible: true,
	};
	if (type === "metal") return { ...base, type: "metal", roughness: 0.1 };
	if (type === "dielectric")
		return { ...base, type: "dielectric", roughness: 0, ior: 1.5 };
	return { ...base, type: "lambertian" };
}

export function AddMaterialDialog({
	existingNames,
	onAdd,
	onCancel,
}: {
	existingNames: string[];
	onAdd: (name: string, mat: Material) => void;
	onCancel: () => void;
}) {
	const [name, setName] = useState("");
	const [type, setType] = useState<Material["type"]>("lambertian");

	const trimmed = name.trim();
	const isDuplicate = existingNames.includes(trimmed);
	const isValid = trimmed !== "" && !isDuplicate;

	function handleCreate() {
		if (!isValid) return;
		onAdd(trimmed, defaultMaterial(type));
	}

	return createPortal(
		// biome-ignore lint/a11y/noStaticElementInteractions: backdrop click-to-close is intentional
		// biome-ignore lint/a11y/useKeyWithClickEvents: Escape is handled by inner dialog inputs
		<div
			className={d.overlay}
			onClick={(e) => e.target === e.currentTarget && onCancel()}
		>
			<div className={d.dialog}>
				<div className={d.header}>
					<span className={`${u.layerTag} ${u.layerTagCtx}`}>MAT</span>
					<span className={d.title}>New Material</span>
				</div>

				<div className={d.body}>
					<div className={`${u.kvTable} ${d.fields}`}>
						<div className={u.kvRow}>
							<span className={u.kvKey}>name</span>
							<input
								className={u.textInput}
								type="text"
								placeholder="material_name"
								value={name}
								// biome-ignore lint/a11y/noAutofocus: intentional — dialog just opened
								autoFocus
								onChange={(e) => setName(e.target.value)}
								onKeyDown={(e) => e.key === "Enter" && handleCreate()}
							/>
						</div>
						<div className={u.kvRow}>
							<span className={u.kvKey}>type</span>
							<select
								className={u.matSelect}
								value={type}
								onChange={(e) => setType(e.target.value as Material["type"])}
							>
								{MATERIAL_TYPES.map((t) => (
									<option key={t} value={t}>
										{t}
									</option>
								))}
							</select>
						</div>
					</div>
					{isDuplicate && trimmed !== "" && (
						<div className={`${d.hint} ${d.hintError}`}>
							Name already in use.
						</div>
					)}
				</div>

				<div className={d.footer}>
					<button
						type="button"
						className={`${d.btn} ${d.btnCancel}`}
						onClick={onCancel}
					>
						cancel
					</button>
					<button
						type="button"
						className={`${d.btn} ${d.btnConfirm}`}
						onClick={handleCreate}
						disabled={!isValid}
					>
						create
					</button>
				</div>
			</div>
		</div>,
		document.body,
	);
}
