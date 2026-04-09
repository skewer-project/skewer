import { useState } from "react";
import { createPortal } from "react-dom";
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
			className="dialog-overlay"
			onClick={(e) => e.target === e.currentTarget && onCancel()}
		>
			<div className="dialog">
				<div className="dialog-header">
					<span className="layer-tag layer-tag-ctx">MAT</span>
					<span className="dialog-title">New Material</span>
				</div>

				<div className="dialog-body">
					<div className="kv-table dialog-fields">
						<div className="kv-row">
							<span className="kv-key">name</span>
							<input
								className="text-input"
								type="text"
								placeholder="material_name"
								value={name}
								// biome-ignore lint/a11y/noAutofocus: intentional — dialog just opened
								autoFocus
								onChange={(e) => setName(e.target.value)}
								onKeyDown={(e) => e.key === "Enter" && handleCreate()}
							/>
						</div>
						<div className="kv-row">
							<span className="kv-key">type</span>
							<select
								className="mat-select"
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
						<div className="dialog-hint dialog-hint-error">
							Name already in use.
						</div>
					)}
				</div>

				<div className="dialog-footer">
					<button
						type="button"
						className="dialog-btn dialog-btn-cancel"
						onClick={onCancel}
					>
						cancel
					</button>
					<button
						type="button"
						className="dialog-btn dialog-btn-confirm"
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
