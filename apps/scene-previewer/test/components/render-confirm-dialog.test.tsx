import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, render } from "vitest-browser-react";
import { RenderConfirmDialog } from "../../src/components/RenderConfirmDialog";
import type { RenderConfig } from "../../src/types/scene";

const settings: RenderConfig = {
	integrator: "path_trace",
	max_samples: 128,
	min_samples: 16,
	max_depth: 8,
	threads: 0,
	image: { width: 1920, height: 1080 },
};

afterEach(async () => {
	await cleanup();
});

describe("RenderConfirmDialog", () => {
	it("summarizes static render settings and confirms", async () => {
		const onConfirm = vi.fn();
		const onCancel = vi.fn();
		const screen = await render(
			<RenderConfirmDialog
				settings={settings}
				startTime={1}
				endTime={1}
				fps={24}
				onConfirm={onConfirm}
				onCancel={onCancel}
			/>,
		);

		await expect
			.element(screen.getByText("Confirm Cloud Render"))
			.toBeVisible();
		await expect.element(screen.getByText("1920 × 1080")).toBeVisible();
		await expect.element(screen.getByText("16 to 128")).toBeVisible();
		await expect.element(screen.getByText("24")).toBeVisible();

		await screen.getByRole("button", { name: "confirm" }).click();

		expect(onConfirm).toHaveBeenCalledOnce();
		expect(onCancel).not.toHaveBeenCalled();
	});

	it("shows animation frame range when end frame is after start frame", async () => {
		const screen = await render(
			<RenderConfirmDialog
				settings={settings}
				startTime={0.5}
				endTime={2}
				fps={24}
				onConfirm={vi.fn()}
				onCancel={vi.fn()}
			/>,
		);

		await expect
			.element(
				screen.getByText(
					"Confirm render animation with the following settings:",
				),
			)
			.toBeVisible();
		await expect.element(screen.getByText("12 to 48")).toBeVisible();
		await expect.element(screen.getByText("0.5s to 2s")).toBeVisible();
	});
});
