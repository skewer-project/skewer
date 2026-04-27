import { Pause, Play } from "lucide-react";
import { useCallback, useEffect, useRef } from "react";
import t from "./Timeline.module.css";

export interface TimelineProps {
	currentTime: number;
	onTimeChange: (time: number) => void;
	isPlaying: boolean;
	onTogglePlay: () => void;
	animRange: { start: number; end: number };
	/** Unique keyframe times across all animated nodes (diamond markers). */
	keyframeTimes?: number[];
}

export function Timeline({
	currentTime,
	onTimeChange,
	isPlaying,
	onTogglePlay,
	animRange,
	keyframeTimes,
}: TimelineProps) {
	const trackRef = useRef<HTMLDivElement>(null);
	const draggingRef = useRef(false);

	const span = animRange.end - animRange.start;
	const hasSpan = span > 1e-6;

	const timeFromClientX = useCallback(
		(clientX: number) => {
			const el = trackRef.current;
			if (!el) return animRange.start;
			const r = el.getBoundingClientRect();
			const w = r.width;
			const x = Math.min(w, Math.max(0, clientX - r.left));
			const u = w > 0 ? x / w : 0;
			if (!hasSpan) return animRange.start;
			return animRange.start + u * span;
		},
		[animRange.start, hasSpan, span],
	);

	const playheadFrac = hasSpan ? (currentTime - animRange.start) / span : 0;
	const playheadPct = Math.min(100, Math.max(0, playheadFrac * 100));

	useEffect(() => {
		function onMove(e: PointerEvent) {
			if (!draggingRef.current) return;
			onTimeChange(timeFromClientX(e.clientX));
		}
		function onUp() {
			draggingRef.current = false;
		}
		window.addEventListener("pointermove", onMove);
		window.addEventListener("pointerup", onUp);
		window.addEventListener("pointercancel", onUp);
		return () => {
			window.removeEventListener("pointermove", onMove);
			window.removeEventListener("pointerup", onUp);
			window.removeEventListener("pointercancel", onUp);
		};
	}, [onTimeChange, timeFromClientX]);

	const onTrackPointerDown = (e: React.PointerEvent) => {
		e.currentTarget.setPointerCapture(e.pointerId);
		draggingRef.current = true;
		onTimeChange(timeFromClientX(e.clientX));
	};

	const onPlayPointerDown = (e: React.PointerEvent) => {
		e.stopPropagation();
	};

	return (
		<div className={`${t.timeline} panel`}>
			<div className={t.controls}>
				<button
					type="button"
					className={t.playBtn}
					onPointerDown={onPlayPointerDown}
					onClick={onTogglePlay}
					aria-label={isPlaying ? "Pause" : "Play"}
					title={isPlaying ? "Pause (Space)" : "Play (Space)"}
				>
					{isPlaying ? (
						<Pause size={14} strokeWidth={2} aria-hidden />
					) : (
						<Play size={14} strokeWidth={2} aria-hidden />
					)}
				</button>
				<span className={t.time}>{currentTime.toFixed(2)}s</span>
			</div>
			<div
				ref={trackRef}
				className={t.track}
				onPointerDown={onTrackPointerDown}
				role="slider"
				tabIndex={0}
				aria-valuemin={animRange.start}
				aria-valuemax={animRange.end}
				aria-valuenow={currentTime}
				aria-label="Animation timeline"
			>
				<div className={t.trackInner}>
					{hasSpan &&
						keyframeTimes?.map((tk) => {
							const f = (tk - animRange.start) / span;
							if (f < 0 || f > 1) return null;
							return (
								<div
									key={`kf-marker-${tk}`}
									className={t.kfMarker}
									style={{ left: `${f * 100}%` }}
								/>
							);
						})}
					<div className={t.playhead} style={{ left: `${playheadPct}%` }} />
				</div>
			</div>
			<span className={t.endLabel}>
				{hasSpan ? `${animRange.end.toFixed(2)}s` : "—"}
			</span>
		</div>
	);
}
