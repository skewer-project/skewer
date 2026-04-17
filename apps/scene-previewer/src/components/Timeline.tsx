import { useState } from "react";
import type { AnimatedNodeTrack } from "../services/transform";
import { TimelineDopeSheet } from "./TimelineDopeSheet";
import { TimelineScrubber } from "./TimelineScrubber";

export interface TimelineProps {
	currentTime: number;
	onTimeChange: (time: number) => void;
	isPlaying: boolean;
	onTogglePlay: () => void;
	animRange: { start: number; end: number };
	/** Unique keyframe times across all animated nodes (scrubber header diamonds). */
	keyframeTimes?: number[];
	/** Per-node tracks for the dope-sheet panel. */
	tracks?: AnimatedNodeTrack[];
}

export function Timeline({
	currentTime,
	onTimeChange,
	isPlaying,
	onTogglePlay,
	animRange,
	keyframeTimes,
	tracks,
}: TimelineProps) {
	const [expanded, setExpanded] = useState(false);

	return (
		<div className="timeline-shell">
			{expanded && (
				<TimelineDopeSheet
					tracks={tracks ?? []}
					animRange={animRange}
					currentTime={currentTime}
				/>
			)}
			<TimelineScrubber
				currentTime={currentTime}
				onTimeChange={onTimeChange}
				isPlaying={isPlaying}
				onTogglePlay={onTogglePlay}
				animRange={animRange}
				keyframeTimes={keyframeTimes}
				expanded={expanded}
				onToggleExpand={() => setExpanded((v) => !v)}
			/>
		</div>
	);
}
