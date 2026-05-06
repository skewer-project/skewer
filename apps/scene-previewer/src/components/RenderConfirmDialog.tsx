import { createPortal } from "react-dom";
import d from "../styles/shared/dialogs.module.css";
import u from "../styles/shared/uiPrimitives.module.css";
import type { RenderConfig } from "../types/scene";

export function RenderConfirmDialog({
	settings,
	startTime,
	endTime,
	fps,
	onConfirm,
	onCancel,
}: {
	settings: RenderConfig;
	startTime: number;
	endTime: number;
	fps: number;
	onConfirm: () => void;
	onCancel: () => void;
}) {
	const startFrame = Math.round(startTime * fps);
	const endFrame = Math.round(endTime * fps);
	const isAnimation = endFrame > startFrame;

	return createPortal(
		// biome-ignore lint/a11y/noStaticElementInteractions: backdrop click-to-close is intentional
		// biome-ignore lint/a11y/useKeyWithClickEvents: Escape is handled by inner dialog inputs
		<div
			className={d.overlay}
			onClick={(e) => e.target === e.currentTarget && onCancel()}
		>
			<div className={d.dialog}>
				<div className={d.header}>
					<span className={`${u.layerTag} ${u.layerTagCtx}`}>RENDER</span>
					<span className={d.title}>Confirm Cloud Render</span>
				</div>

				<div className={d.body}>
					<div
						className={d.hint}
						style={{ marginBottom: "20px", textAlign: "left" }}
					>
						Confirm render {isAnimation ? "animation" : "image"} with the
						following settings:
					</div>

					<div
						className={u.kvTable}
						style={{
							display: "flex",
							flexDirection: "column",
							gap: "8px",
							width: "100%",
						}}
					>
						<div className={u.kvRow} style={{ gridTemplateColumns: "1fr 1fr" }}>
							<span className={u.kvKey} style={{ textAlign: "left" }}>
								dimensions
							</span>
							<span className={u.kvVal} style={{ textAlign: "right" }}>
								{settings.image.width} × {settings.image.height}
							</span>
						</div>
						<div className={u.kvRow} style={{ gridTemplateColumns: "1fr 1fr" }}>
							<span className={u.kvKey} style={{ textAlign: "left" }}>
								sampling
							</span>
							<span className={u.kvVal} style={{ textAlign: "right" }}>
								{settings.min_samples ?? 16} to {settings.max_samples}
							</span>
						</div>
						{isAnimation ? (
							<>
								<div
									className={u.kvRow}
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className={u.kvKey} style={{ textAlign: "left" }}>
										time range
									</span>
									<span className={u.kvVal} style={{ textAlign: "right" }}>
										{startTime}s to {endTime}s
									</span>
								</div>
								<div
									className={u.kvRow}
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className={u.kvKey} style={{ textAlign: "left" }}>
										frame range
									</span>
									<span className={u.kvVal} style={{ textAlign: "right" }}>
										{startFrame} to {endFrame}
									</span>
								</div>
								<div
									className={u.kvRow}
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className={u.kvKey} style={{ textAlign: "left" }}>
										fps
									</span>
									<span className={u.kvVal} style={{ textAlign: "right" }}>
										{fps}
									</span>
								</div>
							</>
						) : (
							<>
								<div
									className={u.kvRow}
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className={u.kvKey} style={{ textAlign: "left" }}>
										time
									</span>
									<span className={u.kvVal} style={{ textAlign: "right" }}>
										{startTime}s
									</span>
								</div>
								<div
									className={u.kvRow}
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className={u.kvKey} style={{ textAlign: "left" }}>
										frame
									</span>
									<span className={u.kvVal} style={{ textAlign: "right" }}>
										{startFrame}
									</span>
								</div>
							</>
						)}
					</div>

					<div className={d.hint} style={{ marginTop: "24px", opacity: 0.6 }}>
						The job will be dispatched to the cloud coordinator.
					</div>
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
						onClick={onConfirm}
					>
						confirm
					</button>
				</div>
			</div>
		</div>,
		document.body,
	);
}
