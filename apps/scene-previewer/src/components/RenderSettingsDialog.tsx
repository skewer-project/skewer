import { useState } from "react";
import { createPortal } from "react-dom";
import type { RenderConfig, ResolvedScene } from "../types/scene";
import { NumberField, Toggle } from "./controls";

export function RenderSettingsDialog({
	scene,
	onRender,
	onCancel,
}: {
	scene: ResolvedScene;
	onRender: (config: RenderConfig) => void;
	onCancel: () => void;
}) {
	// Default values derived from scene or sensible defaults
	const [width, setWidth] = useState(1920);
	const [height, setHeight] = useState(1080);
	const [maxSamples, setMaxSamples] = useState(128);
	const [minSamples, setMinSamples] = useState(16);
	const [maxDepth, setMaxDepth] = useState(8);
	const [threads, setThreads] = useState(0); // 0 = all cores
	const [noiseThreshold, setNoiseThreshold] = useState(0.01);
	const [enableDeep, setEnableDeep] = useState(false);
	const [numFrames, setNumFrames] = useState(1);

	function handleSubmit() {
		const config: RenderConfig = {
			integrator: "path_trace",
			max_samples: maxSamples,
			min_samples: minSamples,
			max_depth: maxDepth,
			threads: threads,
			noise_threshold: noiseThreshold,
			enable_deep: enableDeep,
			image: {
				width: width,
				height: height,
			},
		};
		// For now we just log it as requested by the user's PR focus on UI
		console.log("Cloud Render Job Submitted:", { config, numFrames });
		onRender(config);
	}

	return createPortal(
		<div
			className="dialog-overlay"
			onClick={(e) => e.target === e.currentTarget && onCancel()}
		>
			<div className="dialog">
				<div className="dialog-header">
					<span className="layer-tag layer-tag-ctx">RENDER</span>
					<span className="dialog-title">Cloud Render Settings</span>
				</div>

				<div className="dialog-body">
					<div className="inspector-section-head">Image Dimensions</div>
					<div className="kv-table dialog-fields" style={{ padding: "8px 0" }}>
						<NumberField
							label="width"
							value={width}
							min={1}
							step={1}
							onChange={setWidth}
						/>
						<NumberField
							label="height"
							value={height}
							min={1}
							step={1}
							onChange={setHeight}
						/>
					</div>

					<div className="inspector-section-head">Sampling</div>
					<div className="kv-table dialog-fields" style={{ padding: "8px 0" }}>
						<NumberField
							label="max s."
							value={maxSamples}
							min={1}
							step={1}
							onChange={setMaxSamples}
						/>
						<NumberField
							label="min s."
							value={minSamples}
							min={1}
							step={1}
							onChange={setMinSamples}
						/>
						<NumberField
							label="noise"
							value={noiseThreshold}
							min={0}
							step={0.001}
							onChange={setNoiseThreshold}
						/>
					</div>

					<div className="inspector-section-head">Output</div>
					<div className="kv-table dialog-fields" style={{ padding: "8px 0" }}>
						<NumberField
							label="frames"
							value={numFrames}
							min={1}
							step={1}
							onChange={setNumFrames}
						/>
						<Toggle label="deep" value={enableDeep} onChange={setEnableDeep} />
						<NumberField
							label="depth"
							value={maxDepth}
							min={1}
							step={1}
							onChange={setMaxDepth}
						/>
						<NumberField
							label="threads"
							value={threads}
							min={0}
							step={1}
							onChange={setThreads}
						/>
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
						onClick={handleSubmit}
					>
						start render
					</button>
				</div>
			</div>
		</div>,
		document.body,
	);
}
