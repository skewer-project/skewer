import { useEffect, useRef, useState } from "react";
import type { AnimatedNodeTrack } from "../services/transform";
import { TimelineGroupRow } from "./TimelineGroupRow";
import { TimelineRow } from "./TimelineRow";

/** Must match --timeline-track-inset CSS var and the 220px label column. */
const LABEL_WIDTH_PX = 220;
const TRACK_INSET_PX = 14;
const ZOOM_FACTOR = 1.15;
const MIN_SPAN = 0.05;
const MAX_SPAN_MULTIPLIER = 8;

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
	const animSpan = animRange.end - animRange.start;

	// viewRange is the currently visible time window (may be zoomed/panned).
	const [viewRange, setViewRange] = useState(() => animRange);

	// Reset to full range whenever the scene changes (animRange identity changes).
	useEffect(() => {
		setViewRange(animRange);
	// eslint-disable-next-line react-hooks/exhaustive-deps
	}, [animRange.start, animRange.end]);

	const viewSpan = viewRange.end - viewRange.start;
	const viewHasSpan = viewSpan > 1e-6;

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

	// Playhead fraction uses viewRange so it tracks zoom/pan.
	const playheadFrac = viewHasSpan
		? (currentTime - viewRange.start) / viewSpan
		: 0;

	/** A track row is visible only if none of its ancestor groups are collapsed. */
	function isVisible(track: AnimatedNodeTrack): boolean {
		return track.ancestorKeys.every((k) => !collapsedGroups.has(k));
	}

	// Non-passive wheel handler so we can preventDefault and stop page scroll.
	const innerRef = useRef<HTMLDivElement>(null);
	useEffect(() => {
		const el = innerRef.current;
		if (!el) return;

		function onWheel(e: WheelEvent) {
			e.preventDefault();
			const rect = el!.getBoundingClientRect();
			const trackLeft = rect.left + LABEL_WIDTH_PX + TRACK_INSET_PX;
			const trackWidth = rect.width - LABEL_WIDTH_PX - 2 * TRACK_INSET_PX;
			if (trackWidth <= 0) return;

			// Time value under the cursor.
			const frac = Math.max(0, Math.min(1, (e.clientX - trackLeft) / trackWidth));

			setViewRange((prev) => {
				const span = prev.end - prev.start;
				const timeAtCursor = prev.start + frac * span;

				const factor = e.deltaY > 0 ? ZOOM_FACTOR : 1 / ZOOM_FACTOR;
				const maxSpan = animSpan > 1e-6 ? animSpan * MAX_SPAN_MULTIPLIER : 100;
				const newSpan = Math.max(MIN_SPAN, Math.min(maxSpan, span * factor));

				const newStart = timeAtCursor - frac * newSpan;
				const newEnd = newStart + newSpan;
				return { start: newStart, end: newEnd };
			});
		}

		el.addEventListener("wheel", onWheel, { passive: false });
		return () => el.removeEventListener("wheel", onWheel);
	// animSpan is a derived number; including it keeps the max-span cap current.
	}, [animSpan]);

	return (
		<div className="timeline-dope-sheet panel">
			<div className="timeline-dope-inner" ref={innerRef}>
				{/* Full-height playhead overlay aligned to the track column. */}
				{viewHasSpan && (
					<div
						className="timeline-dope-playhead"
						style={{ "--playhead-frac": playheadFrac } as React.CSSProperties}
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
									viewRange={viewRange}
									span={viewSpan}
									hasSpan={viewHasSpan}
								/>
							);
						}
						return (
							<TimelineRow
								key={track.key}
								track={track}
								viewRange={viewRange}
								span={viewSpan}
								hasSpan={viewHasSpan}
							/>
						);
					})
				)}
			</div>
		</div>
	);
}
