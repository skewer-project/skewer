import { kindShortFromKind } from "../services/node-labels";
import type { AnimatedNodeTrack } from "../services/transform";
import { KeyframeMarker } from "./KeyframeMarker";

export interface TimelineRowProps {
	track: AnimatedNodeTrack;
	animRange: { start: number; end: number };
	span: number;
	hasSpan: boolean;
	/** Placeholder for future per-property graph view. */
	mode?: "dope-sheet" | "graph";
}

export function TimelineRow({
	track,
	animRange,
	span,
	hasSpan,
}: TimelineRowProps) {
	const indent = track.depth * 14;

	return (
		<div className="timeline-row">
			<div
				className="timeline-row-label"
				style={{ paddingLeft: 8 + indent }}
				title={track.label}
			>
				<span className="timeline-row-leaf-spacer" aria-hidden />
				<span className="timeline-row-kind-badge">{kindShortFromKind(track.kind)}</span>
				<span className="timeline-row-name">{track.label}</span>
			</div>
			<div className="timeline-row-track">
				{hasSpan &&
					track.keyframes.map((kf) => {
						const f = (kf.time - animRange.start) / span;
						if (f < 0 || f > 1) return null;
						return (
							<KeyframeMarker
								key={`row-kf-${kf.time}`}
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
