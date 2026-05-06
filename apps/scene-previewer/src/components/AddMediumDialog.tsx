import { useState } from "react";
import { createPortal } from "react-dom";
import { writeFile } from "../services/fs";
import d from "../styles/shared/dialogs.module.css";
import u from "../styles/shared/uiPrimitives.module.css";
import type { Medium } from "../types/scene";

function defaultMedium(type: Medium["type"], file: string): Medium {
	if (type === "nanovdb") {
		return {
			type: "nanovdb",
			sigma_a: [0.1, 0.1, 0.1],
			sigma_s: [0.5, 0.5, 0.5],
			g: 0,
			density_multiplier: 1,
			file,
		};
	}
	throw new Error(`Unknown medium type: ${type}`);
}

export function AddMediumDialog({
	existingNames,
	dirHandle,
	onAdd,
	onCancel,
}: {
	existingNames: string[];
	dirHandle: FileSystemDirectoryHandle;
	onAdd: (name: string, medium: Medium) => void;
	onCancel: () => void;
}) {
	const [name, setName] = useState("");
	const [type] = useState<Medium["type"]>("nanovdb");
	const [file, setFile] = useState("");
	const [fileError, setFileError] = useState<string | null>(null);
	const [copying, setCopying] = useState(false);

	const trimmed = name.trim();
	const isDuplicate = existingNames.includes(trimmed);
	const isValid = trimmed !== "" && !isDuplicate && file !== "";

	async function handleBrowseVdb() {
		setFileError(null);
		try {
			const [fileHandle] = await showOpenFilePicker({
				types: [
					{
						description: "NanoVDB Files",
						accept: { "application/octet-stream": [".nvdb"] },
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
			const buffer = await picked.arrayBuffer();
			const destPath = `vdb/${picked.name}`;
			await writeFile(dirHandle, destPath, buffer);
			setFile(destPath);
		} catch (err) {
			if (err instanceof Error && err.name !== "AbortError") {
				setFileError(`Failed to copy file: ${err.message}`);
			}
		} finally {
			setCopying(false);
		}
	}

	function handleCreate() {
		if (!isValid) return;
		onAdd(trimmed, defaultMedium(type, file));
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
					<span className={`${u.layerTag} ${u.layerTagMed}`}>MED</span>
					<span className={d.title}>New Medium</span>
				</div>

				<div className={d.body}>
					<div className={`${u.kvTable} ${d.fields}`}>
						<div className={u.kvRow}>
							<span className={u.kvKey}>name</span>
							<input
								className={u.textInput}
								type="text"
								placeholder="medium_name"
								value={name}
								// biome-ignore lint/a11y/noAutofocus: intentional — dialog just opened
								autoFocus
								onChange={(e) => setName(e.target.value)}
								onKeyDown={(e) => e.key === "Enter" && handleCreate()}
							/>
						</div>
						<div className={u.kvRow}>
							<span className={u.kvKey}>type</span>
							<select className={u.matSelect} value={type} disabled>
								<option value="nanovdb">nanovdb</option>
							</select>
						</div>
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
									onClick={handleBrowseVdb}
									disabled={copying}
								>
									{copying ? "copying…" : "browse"}
								</button>
							</div>
						</div>
					</div>
					{fileError && (
						<div className={`${d.hint} ${d.hintError}`}>{fileError}</div>
					)}
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
