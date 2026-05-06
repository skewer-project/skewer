import { useState } from "react";
import { createPortal } from "react-dom";
import { writeFile } from "../services/fs";
import d from "../styles/shared/dialogs.module.css";
import u from "../styles/shared/uiPrimitives.module.css";
import type { ObjNode, SceneNode, Vec3 } from "../types/scene";
import { MaterialDropdown, NumberField, Toggle, Vec3Field } from "./controls";

const OBJECT_TYPES = ["group", "sphere", "quad", "obj"] as const;
type AddableType = (typeof OBJECT_TYPES)[number];

export function AddObjectDialog({
	parentKey,
	materialNames,
	dirHandle,
	onAdd,
	onCancel,
}: {
	parentKey: string;
	materialNames: string[];
	dirHandle: FileSystemDirectoryHandle;
	onAdd: (obj: SceneNode) => void;
	onCancel: () => void;
}) {
	const [type, setType] = useState<AddableType>("sphere");
	const [nodeName, setNodeName] = useState("");

	const [center, setCenter] = useState<Vec3>([0, 0, 0]);
	const [radius, setRadius] = useState(1);

	const [verts, setVerts] = useState<[Vec3, Vec3, Vec3, Vec3]>([
		[-1, 0, -1],
		[1, 0, -1],
		[1, 0, 1],
		[-1, 0, 1],
	]);

	const [file, setFile] = useState("");
	const [fileError, setFileError] = useState<string | null>(null);
	const [copying, setCopying] = useState(false);
	const [autoFit, setAutoFit] = useState(true);

	const [material, setMaterial] = useState(materialNames[0] ?? "");

	const needsMaterial = type === "sphere" || type === "quad";
	const hasMaterial = materialNames.length > 0;
	const canAdd =
		(type === "obj"
			? file.trim() !== ""
			: type === "group"
				? true
				: hasMaterial) && (needsMaterial ? hasMaterial : true);

	function attachName<N extends SceneNode>(n: N): N {
		const name = nodeName.trim();
		if (name) (n as { name?: string }).name = name;
		return n;
	}

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

			const parts = await dirHandle.resolve(fileHandle);
			if (parts) {
				setFile(parts.join("/"));
				return;
			}

			setCopying(true);
			const picked = await fileHandle.getFile();
			const content = await picked.text();
			const destPath = `models/${picked.name}`;
			await writeFile(dirHandle, destPath, content);
			setFile(destPath);
		} catch (err) {
			if (err instanceof Error && err.name !== "AbortError") {
				setFileError(`Failed to copy file: ${err.message}`);
			}
		} finally {
			setCopying(false);
		}
	}

	function handleAdd() {
		if (!canAdd) return;
		if (type === "group") {
			onAdd(attachName({ kind: "group", children: [] }));
		} else if (type === "sphere") {
			onAdd(
				attachName({
					kind: "sphere",
					material: material,
					center,
					radius,
				}),
			);
		} else if (type === "quad") {
			onAdd(
				attachName({
					kind: "quad",
					material: material,
					vertices: verts,
				}),
			);
		} else {
			let obj: ObjNode = {
				kind: "obj",
				file: file.trim(),
				auto_fit: autoFit,
			};
			if (material && hasMaterial) obj.material = material;
			obj = attachName(obj);
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
			className={d.overlay}
			onClick={(e) => e.target === e.currentTarget && onCancel()}
		>
			<div className={d.dialog}>
				<div className={d.header}>
					<span className={`${u.layerTag} ${u.layerTagLyr}`}>NODE</span>
					<span className={d.title}>Add node</span>
				</div>
				<div className={d.hint} style={{ padding: "0 1rem", opacity: 0.8 }}>
					Parent: {parentKey}
				</div>

				<div className={d.body}>
					<div className={`${u.kvRow} ${d.typeRow}`}>
						<span className={u.kvKey}>name</span>
						<input
							className={u.matSelect}
							style={{ flex: 1 }}
							value={nodeName}
							placeholder="optional"
							onChange={(e) => setNodeName(e.target.value)}
						/>
					</div>
					<div className={`${u.kvRow} ${d.typeRow}`}>
						<span className={u.kvKey}>type</span>
						<select
							className={u.matSelect}
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

					<div className={`${u.kvTable} ${d.fields}`}>
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
								<div className={u.kvRow}>
									<span className={u.kvKey}>file</span>
									<div className={u.objFileRow}>
										<span className={u.objFileName}>
											{file || (
												<span className={u.objFileEmpty}>no file selected</span>
											)}
										</span>
										<button
											type="button"
											className={`${u.objBrowseBtn}${copying ? " loading" : ""}`}
											onClick={handleBrowseObj}
											disabled={copying}
										>
											{copying ? "copying…" : "browse"}
										</button>
									</div>
								</div>
								{fileError && (
									<div className={`${d.hint} ${d.hintError}`}>{fileError}</div>
								)}
								<Toggle label="fit" value={autoFit} onChange={setAutoFit} />
							</>
						)}
					</div>

					{hasMaterial && type !== "group" && (
						<div className={`${u.kvTable} ${d.fields}`}>
							<MaterialDropdown
								label="mat"
								value={material}
								options={materialNames}
								onChange={setMaterial}
							/>
						</div>
					)}

					{needsMaterial && !hasMaterial && (
						<div className={d.hint}>
							Create a material first to use with this object.
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
