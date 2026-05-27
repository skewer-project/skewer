import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, render } from "vitest-browser-react";
import { Timeline } from "../../src/components/Timeline";
import type { AnimatedNodeTrack } from "../../src/services/transform";

const tracks: AnimatedNodeTrack[] = [
	{
		key: "layer:0/0",
		label: "Animated sphere",
		kind: "sphere",
		depth: 0,
		ancestorKeys: [],
		keyframes: [{ time: 0 }, { time: 1 }],
		layerIdx: 1,
		layerTag: "lyr",
		layerName: "sphere",
	},
];

afterEach(async () => {
	await cleanup();
});

describe("Timeline", () => {
	it("toggles playback from the scrubber", async () => {
		const onTogglePlay = vi.fn();
		const screen = await render(
			<Timeline
				currentTime={0}
				onTimeChange={vi.fn()}
				isPlaying={false}
				onTogglePlay={onTogglePlay}
				animRange={{ start: 0, end: 2 }}
			/>,
		);

		await screen.getByRole("button", { name: "Play" }).click();

		expect(onTogglePlay).toHaveBeenCalledOnce();
	});

	it("opens the dope sheet and selects a keyed object", async () => {
		const onTimeChange = vi.fn();
		const onSelectObject = vi.fn();
		const screen = await render(
			<Timeline
				currentTime={0}
				onTimeChange={onTimeChange}
				isPlaying={false}
				onTogglePlay={vi.fn()}
				animRange={{ start: 0, end: 2 }}
				keyframeTimes={[0, 1]}
				tracks={tracks}
				onSelectObject={onSelectObject}
			/>,
		);

		await screen.getByRole("button", { name: "Expand dope-sheet" }).click();
		await expect.element(screen.getByText("Animated sphere")).toBeVisible();

		const marker =
			screen.container.querySelector<HTMLElement>('[title="1.00s"]');
		expect(marker).not.toBeNull();
		marker?.dispatchEvent(
			new PointerEvent("pointerdown", {
				bubbles: true,
				clientX: 100,
				pointerId: 1,
			}),
		);

		expect(onTimeChange).toHaveBeenCalledWith(1);
		expect(onSelectObject).toHaveBeenCalledWith("layer:0/0");
	});
});
