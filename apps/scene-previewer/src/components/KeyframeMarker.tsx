/**
 * Shared diamond marker used in both the scrubber header track (variant="header")
 * and per-node dope-sheet rows (variant="row"). Positioned by the parent as a
 * percentage along the time axis.
 */
export function KeyframeMarker({
	fraction,
	variant,
	title,
	dragging,
	onPointerDown,
}: {
	fraction: number;
	variant: "header" | "row";
	title?: string;
	/** True while this specific marker is being dragged (highlights it). */
	dragging?: boolean;
	onPointerDown?: React.PointerEventHandler<HTMLDivElement>;
}) {
	const base = variant === "header" ? "timeline-kf-marker" : "timeline-row-kf";
	const cls = [
		base,
		onPointerDown ? "draggable" : "",
		dragging ? "dragging" : "",
	]
		.filter(Boolean)
		.join(" ");

	// Row markers live in .timeline-row-track (position:relative, padding:0 14px).
	// Absolutely-positioned children ignore padding, so left:0% = track column left edge.
	// Use an inset calc so the marker center matches the dope-sheet playhead coord system.
	// Header markers live in .timeline-track-inner which is already inside the 14px padding.
	const left =
		variant === "row"
			? `calc(14px + ${fraction} * (100% - 28px))`
			: `${fraction * 100}%`;

	return (
		<div
			className={cls}
			style={{ left }}
			title={title}
			onPointerDown={onPointerDown}
		/>
	);
}
