import { useEffect, useRef, useState } from "react";
import { kindShortFromKind } from "../services/node-labels";
import type { AnimatedNodeTrack } from "../services/transform";
import { KeyframeMarker } from "./KeyframeMarker";

const TRACK_INSET_PX = 14;

interface DragState {
	kfTime: number;
	baseFrac: number;
	startClientX: number;
	previewFrac: number;
}

export interface TimelineRowProps {
	track: AnimatedNodeTrack;
	viewRange: { start: number; end: number };
	span: number;
	hasSpan: boolean;
	onKeyframeMove?: (trackKey: string, oldTime: number, newTime: number) => void;
	/** Called on any keyframe click (before drag); use to select the object + seek time. */
	onKeyframeClick?: (trackKey: string, kfTime: number) => void;
	/** Placeholder for future per-property graph view. */
	mode?: "dope-sheet" | "graph";
}

export function TimelineRow({
	track,
	viewRange,
	span,
	hasSpan,
	onKeyframeMove,
	onKeyframeClick,
}: TimelineRowProps) {
	const indent = track.depth * 14;
	const trackRef = useRef<HTMLDivElement>(null);
	const [drag, setDrag] = useState<DragState | null>(null);

	// Keep mutable values in refs so the stable useEffect closure can read them.
	const dragRef = useRef<DragState | null>(null);
	dragRef.current = drag;
	const viewRangeRef = useRef(viewRange);
	viewRangeRef.current = viewRange;
	const spanRef = useRef(span);
	spanRef.current = span;
	const trackKeyRef = useRef(track.key);
	trackKeyRef.current = track.key;
	const onKeyframeMoveRef = useRef(onKeyframeMove);
	onKeyframeMoveRef.current = onKeyframeMove;
	const onKeyframeClickRef = useRef(onKeyframeClick);
	onKeyframeClickRef.current = onKeyframeClick;

	useEffect(() => {
		function onMove(e: PointerEvent) {
			const d = dragRef.current;
			if (!d) return;
			const trackEl = trackRef.current;
			if (!trackEl) return;
			const rect = trackEl.getBoundingClientRect();
			const trackWidth = rect.width - 2 * TRACK_INSET_PX;
			if (trackWidth <= 0) return;
			const dx = e.clientX - d.startClientX;
			const newFrac = Math.max(0, Math.min(1, d.baseFrac + dx / trackWidth));
			setDrag((prev) => (prev ? { ...prev, previewFrac: newFrac } : null));
		}
		function onUp() {
			const d = dragRef.current;
			if (!d) return;
			const cb = onKeyframeMoveRef.current;
			if (cb) {
				const vr = viewRangeRef.current;
				const sp = spanRef.current;
				cb(trackKeyRef.current, d.kfTime, vr.start + d.previewFrac * sp);
			}
			setDrag(null);
		}
		window.addEventListener("pointermove", onMove);
		window.addEventListener("pointerup", onUp);
		window.addEventListener("pointercancel", onUp);
		return () => {
			window.removeEventListener("pointermove", onMove);
			window.removeEventListener("pointerup", onUp);
			window.removeEventListener("pointercancel", onUp);
		};
	}, []); // stable — all live values accessed via refs

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
			<div className="timeline-row-track" ref={trackRef}>
				{hasSpan &&
					track.keyframes.map((kf) => {
						const realFrac = (kf.time - viewRange.start) / span;
						const isDragging = drag?.kfTime === kf.time;
						if (!isDragging && (realFrac < 0 || realFrac > 1)) return null;
						const displayFrac = isDragging ? drag!.previewFrac : realFrac;
						if (displayFrac < 0 || displayFrac > 1) return null;
						return (
							<KeyframeMarker
								key={`row-kf-${kf.time}`}
								fraction={displayFrac}
								variant="row"
								title={`${kf.time.toFixed(2)}s`}
								dragging={isDragging}
								onPointerDown={
									onKeyframeMove || onKeyframeClick
										? (e) => {
												e.stopPropagation();
												e.currentTarget.setPointerCapture(e.pointerId);
												onKeyframeClickRef.current?.(track.key, kf.time);
												if (onKeyframeMove) {
													setDrag({
														kfTime: kf.time,
														baseFrac: realFrac,
														startClientX: e.clientX,
														previewFrac: realFrac,
													});
												}
											}
										: undefined
								}
							/>
						);
					})}
			</div>
		</div>
	);
}
