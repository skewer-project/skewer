import { useState } from "react";
import { createPortal } from "react-dom";
import { writeTextFile } from "../services/fs";
import type { ObjFileObject, SceneObject, Vec3 } from "../types/scene";
import { MaterialDropdown, NumberField, Toggle, Vec3Field } from "./controls";

const OBJECT_TYPES = ["sphere", "quad", "obj"] as const;
type AddableType = (typeof OBJECT_TYPES)[number];

export function AddObjectDialog({
	materialNames,
	dirHandle,
	onAdd,
	onCancel,
}: {
	materialNames: string[];
	dirHandle: FileSystemDirectoryHandle;
	onAdd: (obj: SceneObject) => void;
	onCancel: () => void;
}) {
	const [type, setType] = useState<AddableType>("sphere");

	// Sphere state
	const [center, setCenter] = useState<Vec3>([0, 0, 0]);
	const [radius, setRadius] = useState(1);

	// Quad state — default: a 2×2 quad lying flat on y=0
	const [verts, setVerts] = useState<[Vec3, Vec3, Vec3, Vec3]>([
		[-1, 0, -1],
		[1, 0, -1],
		[1, 0, 1],
		[-1, 0, 1],
	]);

	// OBJ state
	const [file, setFile] = useState("");
	const [fileError, setFileError] = useState<string | null>(null);
	const [copying, setCopying] = useState(false);
	const [autoFit, setAutoFit] = useState(true);

	// Shared material
	const [material, setMaterial] = useState(materialNames[0] ?? "");

	const needsMaterial = type !== "obj";
	const hasMaterial = materialNames.length > 0;
	const canAdd =
		(type === "obj" ? file.trim() !== "" : hasMaterial) &&
		(needsMaterial ? hasMaterial : true);

	async function handleBrowseObj() {
		setFileError(null);
		try {
			const [fileHandle] = await showOpenFilePicker({
				types: [
					{
						description: "OBJ Files",
						accept: { "model/obj": [".obj"] },
					},
				],
				multiple: false,
			});

			// If the file is already inside the scene folder, use it directly.
			const parts = await dirHandle.resolve(fileHandle);
			if (parts) {
				setFile(parts.join("/"));
				return;
			}

			// Otherwise, copy the file into models/ inside the scene folder.
			setCopying(true);
			const picked = await fileHandle.getFile();
			const content = await picked.text();
			const destPath = `models/${picked.name}`;
			await writeTextFile(dirHandle, destPath, content);
			setFile(destPath);
		} catch (err) {
			// Ignore user cancellation; surface real errors.
			if (err instanceof Error && err.name !== "AbortError") {
				setFileError(`Failed to copy file: ${err.message}`);
			}
		} finally {
			setCopying(false);
		}
	}

	function handleAdd() {
		if (!canAdd) return;
		if (type === "sphere") {
			onAdd({ type: "sphere", material, center, radius });
		} else if (type === "quad") {
			onAdd({ type: "quad", material, vertices: verts });
		} else {
			const obj: ObjFileObject = {
				type: "obj",
				file: file.trim(),
				auto_fit: autoFit,
			};
			if (material && hasMaterial) obj.material = material;
			onAdd(obj);
		}
	}

	function handleVertexChange(idx: number, v: Vec3) {
		const next = [...verts] as typeof verts;
		next[idx] = v;
		setVerts(next);
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
					<span className="layer-tag layer-tag-lyr">OBJ</span>
					<span className="dialog-title">Add Object</span>
				</div>

				<div className="dialog-body">
					{/* Type selector */}
					<div className="kv-row dialog-type-row">
						<span className="kv-key">type</span>
						<select
							className="mat-select"
							value={type}
							onChange={(e) => setType(e.target.value as AddableType)}
						>
							{OBJECT_TYPES.map((t) => (
								<option key={t} value={t}>
									{t}
								</option>
							))}
						</select>
					</div>

					<div className="kv-table dialog-fields">
						{type === "sphere" && (
							<>
								<Vec3Field label="center" value={center} onChange={setCenter} />
								<NumberField
									label="radius"
									value={radius}
									min={0.001}
									step={0.1}
									onChange={setRadius}
								/>
							</>
						)}

						{type === "quad" &&
							(["p0", "p1", "p2", "p3"] as const).map((label, i) => (
								<Vec3Field
									key={label}
									label={label}
									value={verts[i]}
									onChange={(v) => handleVertexChange(i, v)}
								/>
							))}

						{type === "obj" && (
							<>
								<div className="kv-row">
									<span className="kv-key">file</span>
									<div className="obj-file-row">
										<span className="obj-file-name">
											{file || (
												<span className="obj-file-empty">no file selected</span>
											)}
										</span>
										<button
											type="button"
											className={`obj-browse-btn${copying ? " loading" : ""}`}
											onClick={handleBrowseObj}
											disabled={copying}
										>
											{copying ? "copying…" : "browse"}
										</button>
									</div>
								</div>
								{fileError && (
									<div className="dialog-hint dialog-hint-error">
										{fileError}
									</div>
								)}
								<Toggle label="fit" value={autoFit} onChange={setAutoFit} />
							</>
						)}

						{hasMaterial && (
							<MaterialDropdown
								label="mat"
								value={material}
								options={materialNames}
								onChange={setMaterial}
							/>
						)}
					</div>

					{needsMaterial && !hasMaterial && (
						<div className="dialog-hint">
							Create a material first to use with this object.
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
						onClick={handleAdd}
						disabled={!canAdd}
					>
						add
					</button>
				</div>
			</div>
		</div>,
		document.body,
	);
}
