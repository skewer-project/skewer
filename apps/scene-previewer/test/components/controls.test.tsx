import { useState } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { page, userEvent } from "vitest/browser";
import { cleanup, render } from "vitest-browser-react";
import { NumberField, Toggle, Vec3Field } from "../../src/components/controls";

afterEach(async () => {
	await cleanup();
});

describe("NumberField", () => {
	it("commits clamped integer edits on blur", async () => {
		const onChange = vi.fn();
		const screen = await render(
			<StatefulNumberField
				label="samples"
				initialValue={4}
				onChange={onChange}
				min={1}
				max={8}
				integer
			/>,
		);

		const input = screen.getByRole("spinbutton");
		await input.fill("12.6");
		await userEvent.tab();

		expect(onChange).toHaveBeenLastCalledWith(8);
		await expect.element(input).toHaveValue(8);
	});

	it("steps with arrow keys using the configured step", async () => {
		const onChange = vi.fn();
		const screen = await render(
			<StatefulNumberField
				label="depth"
				initialValue={2}
				onChange={onChange}
				step={0.25}
			/>,
		);

		const input = screen.getByRole("spinbutton");
		await input.click();
		await userEvent.keyboard("{ArrowUp}");
		await userEvent.keyboard("{ArrowDown}");

		expect(onChange).toHaveBeenNthCalledWith(1, 2.25);
		expect(onChange).toHaveBeenNthCalledWith(2, 2);
	});
});

describe("Vec3Field", () => {
	it("updates only the edited component", async () => {
		const onChange = vi.fn();
		const screen = await render(
			<Vec3Field label="translate" value={[1, 2, 3]} onChange={onChange} />,
		);

		const inputs = screen.container.querySelectorAll("input");
		expect(inputs).toHaveLength(3);
		await page.elementLocator(inputs[1]).fill("5");
		inputs[1].blur();

		expect(onChange).toHaveBeenLastCalledWith([1, 5, 3]);
	});
});

function StatefulNumberField({
	initialValue,
	onChange,
	...props
}: Omit<React.ComponentProps<typeof NumberField>, "value" | "onChange"> & {
	initialValue: number;
	onChange: (value: number) => void;
}) {
	const [value, setValue] = useState(initialValue);
	return (
		<NumberField
			{...props}
			value={value}
			onChange={(next) => {
				setValue(next);
				onChange(next);
			}}
		/>
	);
}

describe("Toggle", () => {
	it("emits the next boolean value", async () => {
		const onChange = vi.fn();
		const screen = await render(
			<Toggle label="visible" value={false} onChange={onChange} />,
		);

		await screen.getByRole("button", { name: "off" }).click();

		expect(onChange).toHaveBeenCalledWith(true);
	});
});
