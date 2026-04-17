/**
 * Shared diamond marker used in both the scrubber header track (variant="header")
 * and per-node dope-sheet rows (variant="row"). Positioned by the parent as a
 * percentage along the time axis.
 */
export function KeyframeMarker({
	fraction,
	variant,
	title,
}: {
	fraction: number;
	variant: "header" | "row";
	title?: string;
}) {
	return (
		<div
			className={
				variant === "header" ? "timeline-kf-marker" : "timeline-row-kf"
			}
			style={{ left: `${fraction * 100}%` }}
			title={title}
		/>
	);
}
