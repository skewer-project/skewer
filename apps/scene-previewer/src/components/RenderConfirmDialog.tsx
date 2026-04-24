import { createPortal } from "react-dom";
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
			className="dialog-overlay"
			onClick={(e) => e.target === e.currentTarget && onCancel()}
		>
			<div className="dialog render-confirm-dialog">
				<div className="dialog-header">
					<span className="layer-tag layer-tag-ctx">RENDER</span>
					<span className="dialog-title">Confirm Cloud Render</span>
				</div>

				<div className="dialog-body">
					<div
						className="dialog-hint"
						style={{ marginBottom: "20px", textAlign: "left" }}
					>
						Confirm render {isAnimation ? "animation" : "image"} with the
						following settings:
					</div>

					<div
						className="kv-table"
						style={{
							display: "flex",
							flexDirection: "column",
							gap: "8px",
							width: "100%",
						}}
					>
						<div className="kv-row" style={{ gridTemplateColumns: "1fr 1fr" }}>
							<span className="kv-key" style={{ textAlign: "left" }}>
								dimensions
							</span>
							<span className="kv-val" style={{ textAlign: "right" }}>
								{settings.image.width} × {settings.image.height}
							</span>
						</div>
						<div className="kv-row" style={{ gridTemplateColumns: "1fr 1fr" }}>
							<span className="kv-key" style={{ textAlign: "left" }}>
								sampling
							</span>
							<span className="kv-val" style={{ textAlign: "right" }}>
								{settings.min_samples ?? 16} to {settings.max_samples}
							</span>
						</div>
						{isAnimation ? (
							<>
								<div
									className="kv-row"
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className="kv-key" style={{ textAlign: "left" }}>
										time range
									</span>
									<span className="kv-val" style={{ textAlign: "right" }}>
										{startTime}s to {endTime}s
									</span>
								</div>
								<div
									className="kv-row"
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className="kv-key" style={{ textAlign: "left" }}>
										frame range
									</span>
									<span className="kv-val" style={{ textAlign: "right" }}>
										{startFrame} to {endFrame}
									</span>
								</div>
								<div
									className="kv-row"
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className="kv-key" style={{ textAlign: "left" }}>
										fps
									</span>
									<span className="kv-val" style={{ textAlign: "right" }}>
										{fps}
									</span>
								</div>
							</>
						) : (
							<>
								<div
									className="kv-row"
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className="kv-key" style={{ textAlign: "left" }}>
										time
									</span>
									<span className="kv-val" style={{ textAlign: "right" }}>
										{startTime}s
									</span>
								</div>
								<div
									className="kv-row"
									style={{ gridTemplateColumns: "1fr 1fr" }}
								>
									<span className="kv-key" style={{ textAlign: "left" }}>
										frame
									</span>
									<span className="kv-val" style={{ textAlign: "right" }}>
										{startFrame}
									</span>
								</div>
							</>
						)}
					</div>

					<div
						className="dialog-hint"
						style={{ marginTop: "24px", opacity: 0.6 }}
					>
						The job will be dispatched to the cloud coordinator.
					</div>
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
