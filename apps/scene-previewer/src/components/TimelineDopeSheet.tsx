import { useState } from "react";
import type { AnimatedNodeTrack } from "../services/transform";
import { TimelineGroupRow } from "./TimelineGroupRow";
import { TimelineRow } from "./TimelineRow";

export interface TimelineDopeSheetProps {
	tracks: AnimatedNodeTrack[];
	animRange: { start: number; end: number };
	currentTime: number;
}

export function TimelineDopeSheet({
	tracks,
	animRange,
	currentTime,
}: TimelineDopeSheetProps) {
	const span = animRange.end - animRange.start;
	const hasSpan = span > 1e-6;

	// All group keys start expanded.
	const groupKeys = new Set(
		tracks.filter((t) => t.kind === "group").map((t) => t.key),
	);
	const [collapsedGroups, setCollapsedGroups] = useState<Set<string>>(
		() => new Set(),
	);

	const toggleGroup = (key: string) => {
		setCollapsedGroups((prev) => {
			const next = new Set(prev);
			if (next.has(key)) next.delete(key);
			else next.add(key);
			return next;
		});
	};

	const playheadFrac = hasSpan ? (currentTime - animRange.start) / span : 0;
	const playheadPct = Math.min(100, Math.max(0, playheadFrac * 100));

	/** A track row is visible only if none of its ancestor groups are collapsed. */
	function isVisible(track: AnimatedNodeTrack): boolean {
		return track.ancestorKeys.every((k) => !collapsedGroups.has(k));
	}

	return (
		<div className="timeline-dope-sheet panel">
			<div className="timeline-dope-inner">
				{/* Full-height playhead overlay */}
				{hasSpan && (
					<div
						className="timeline-dope-playhead"
						style={{ left: `${playheadPct}%` }}
					/>
				)}

				{tracks.length === 0 ? (
					<div className="timeline-dope-empty">No animated objects</div>
				) : (
					tracks.map((track) => {
						if (!isVisible(track)) return null;
						if (track.kind === "group") {
							return (
								<TimelineGroupRow
									key={track.key}
									track={track}
									collapsed={collapsedGroups.has(track.key)}
									onToggle={() => toggleGroup(track.key)}
									isGroup={groupKeys.has(track.key)}
									animRange={animRange}
									span={span}
									hasSpan={hasSpan}
								/>
							);
						}
						return (
							<TimelineRow
								key={track.key}
								track={track}
								animRange={animRange}
								span={span}
								hasSpan={hasSpan}
							/>
						);
					})
				)}
			</div>
		</div>
	);
}
