import { ChevronRight } from "lucide-react";
import { KeyframeMarker } from "./KeyframeMarker";
import type { AnimatedNodeTrack } from "../services/transform";

export interface TimelineGroupRowProps {
	track: AnimatedNodeTrack;
	collapsed: boolean;
	onToggle: () => void;
	/** True when this group key has child animated tracks. */
	isGroup: boolean;
	viewRange: { start: number; end: number };
	span: number;
	hasSpan: boolean;
}

export function TimelineGroupRow({
	track,
	collapsed,
	onToggle,
	viewRange,
	span,
	hasSpan,
}: TimelineGroupRowProps) {
	const indent = track.depth * 14;

	return (
		<div className="timeline-row timeline-group-row">
			<div
				className="timeline-row-label"
				style={{ paddingLeft: 8 + indent }}
				title={track.label}
			>
				<button
					type="button"
					className="timeline-row-expand"
					onClick={onToggle}
					aria-label={collapsed ? "Expand group" : "Collapse group"}
				>
					<ChevronRight
						className={`timeline-row-expand-icon${collapsed ? "" : " timeline-row-expand-open"}`}
						size={12}
						strokeWidth={2}
						aria-hidden
					/>
				</button>
				<span className="timeline-row-kind-badge">GRP</span>
				<span className="timeline-row-name">{track.label}</span>
			</div>
			<div className="timeline-row-track">
				{/* Show the group's own keyframes if it has any */}
				{hasSpan &&
					track.keyframes.map((kf) => {
						const f = (kf.time - viewRange.start) / span;
						if (f < 0 || f > 1) return null;
						return (
							<KeyframeMarker
								key={`grp-kf-${kf.time}`}
								fraction={f}
								variant="row"
								title={`${kf.time.toFixed(2)}s`}
							/>
						);
					})}
			</div>
		</div>
	);
}
