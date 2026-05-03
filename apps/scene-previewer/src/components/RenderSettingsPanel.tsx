import * as Collapsible from "@radix-ui/react-collapsible";
import { ChevronRight } from "lucide-react";
import { memo } from "react";
import type { RenderConfig } from "../types/scene";
import { NumberField, Vec2Field } from "./controls";

interface RenderSettingsPanelProps {
	settings: RenderConfig;
	onSettingsChange: (s: RenderConfig) => void;
	startTime: number;
	onStartTimeChange: (n: number) => void;
	endTime: number;
	onEndTimeChange: (n: number) => void;
	fps: number;
	onFpsChange: (n: number) => void;
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

	return (
		<Collapsible.Root className="layer-card" defaultOpen>
			<Collapsible.Trigger asChild>
				<button type="button" className="layer-summary">
					<ChevronRight
						className="layer-chevron-icon"
						size={18}
						strokeWidth={2}
						aria-hidden
					/>
					<span className="layer-tag layer-tag-ctx">RENDER</span>
					<span className="layer-name">Settings</span>
				</button>
			</Collapsible.Trigger>

			<Collapsible.Content className="layer-collapsible-content">
				<div className="layer-body">
					<div className="layer-sub-head">Image</div>
					<div className="kv-table">
						<Vec2Field
							label="res"
							value={[settings.image.width, settings.image.height]}
							onChange={updateImage}
							componentLabels={["w", "h"]}
							min={1}
							integer
						/>
					</div>

					<div className="layer-sub-head">Sampling</div>
					<div className="kv-table">
						<Vec2Field
							label="range"
							value={[settings.min_samples ?? 16, settings.max_samples]}
							onChange={(v) => patch({ min_samples: v[0], max_samples: v[1] })}
							componentLabels={["min", "max"]}
							min={1}
							integer
						/>
						<div className="kv-row">
							<span className="kv-key">noise</span>
							<div className="vec3-cell">
								<span className="vec3-component">threshold</span>
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

					<div className="layer-sub-head">Output</div>
					<div className="kv-table">
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
				</div>
			</Collapsible.Content>
		</Collapsible.Root>
	);
});
