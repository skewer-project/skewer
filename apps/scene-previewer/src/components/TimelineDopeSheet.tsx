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
	onTimeChange?: (t: number) => void;
	onKeyframeMove?: (trackKey: string, oldTime: number, newTime: number) => void;
	/** Called when a keyframe is clicked; selects the node in the properties panel. */
	onSelectObject?: (key: string | null) => void;
}

export function TimelineDopeSheet({
	tracks,
	animRange,
	currentTime,
	onTimeChange,
	onKeyframeMove,
	onSelectObject,
}: TimelineDopeSheetProps) {
	const animSpan = animRange.end - animRange.start;

	// viewRange is the currently visible time window (may be zoomed/panned).
	const [viewRange, setViewRange] = useState(() => animRange);
	// Will automatically reset to full range whenever the scene changes (animRange identity changes)
	// because animRange start and end are part of the key.

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

	const innerRef = useRef<HTMLDivElement>(null);

	// Keep viewRange in refs for event handlers so they don't need re-registration.
	const viewRangeRef = useRef(viewRange);
	const viewSpanRef = useRef(viewSpan);
	const viewHasSpanRef = useRef(viewHasSpan);
	useEffect(() => {
		viewRangeRef.current = viewRange;
		viewSpanRef.current = viewSpan;
		viewHasSpanRef.current = viewHasSpan;
	}, [viewRange, viewSpan, viewHasSpan]);

	// Non-passive wheel handler so we can preventDefault and stop page scroll.
	useEffect(() => {
		const el = innerRef.current;
		if (!el) return;

		function onWheel(e: WheelEvent) {
			e.preventDefault();
			const rect = el?.getBoundingClientRect();
			const trackLeft = rect?.left ?? 0 + LABEL_WIDTH_PX + TRACK_INSET_PX;
			const trackWidth = rect?.width ?? 0 - LABEL_WIDTH_PX - 2 * TRACK_INSET_PX;
			if (trackWidth <= 0) return;

			const frac = Math.max(
				0,
				Math.min(1, (e.clientX - trackLeft) / trackWidth),
			);

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

	// Keyframe click: select the object in the properties panel + seek to kf time.
	const handleKeyframeClick = (trackKey: string, kfTime: number) => {
		onTimeChange?.(kfTime);
		onSelectObject?.(trackKey);
	};

	// Click-to-scrub: pointer capture in the track column area.
	const scrubbingRef = useRef(false);
	const onTimeChangeRef = useRef(onTimeChange);

	useEffect(() => {
		onTimeChangeRef.current = onTimeChange;
	}, [onTimeChange]);

	function timeFromClientX(clientX: number): number {
		const el = innerRef.current;
		if (!el) return viewRangeRef.current.start;
		const rect = el.getBoundingClientRect();
		const trackLeft = rect.left + LABEL_WIDTH_PX + TRACK_INSET_PX;
		const trackWidth = rect.width - LABEL_WIDTH_PX - 2 * TRACK_INSET_PX;
		if (trackWidth <= 0) return viewRangeRef.current.start;
		const frac = Math.max(0, Math.min(1, (clientX - trackLeft) / trackWidth));
		return viewRangeRef.current.start + frac * viewSpanRef.current;
	}

	const onInnerPointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
		if (!onTimeChangeRef.current || !viewHasSpanRef.current) return;
		const el = innerRef.current;
		if (!el) return;
		// Only activate in the track column (right of label + inset).
		const rect = el.getBoundingClientRect();
		if (e.clientX < rect.left + LABEL_WIDTH_PX + TRACK_INSET_PX) return;
		e.currentTarget.setPointerCapture(e.pointerId);
		scrubbingRef.current = true;
		onTimeChangeRef.current(timeFromClientX(e.clientX));
	};

	const onInnerPointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
		if (!scrubbingRef.current || !onTimeChangeRef.current) return;
		onTimeChangeRef.current(timeFromClientX(e.clientX));
	};

	const onInnerPointerUp = () => {
		scrubbingRef.current = false;
	};

	return (
		<div className="timeline-dope-sheet panel">
			<div
				className="timeline-dope-inner"
				ref={innerRef}
				onPointerDown={onInnerPointerDown}
				onPointerMove={onInnerPointerMove}
				onPointerUp={onInnerPointerUp}
				onPointerCancel={onInnerPointerUp}
			>
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
									onKeyframeMove={onKeyframeMove}
									onKeyframeClick={handleKeyframeClick}
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
								onKeyframeMove={onKeyframeMove}
								onKeyframeClick={handleKeyframeClick}
							/>
						);
					})
				)}
			</div>
		</div>
	);
}
