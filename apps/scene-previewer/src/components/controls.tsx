import { memo, useCallback, useEffect, useRef, useState } from "react";
import type { Vec3 } from "../types/scene";

// ── NumberField ─────────────────────────────────────────────

interface NumberFieldProps {
	label: string;
	value: number;
	onChange: (v: number) => void;
	min?: number;
	max?: number;
	step?: number;
	/** If true, renders only the input (no label column). Used inside Vec3Field. */
	inline?: boolean;
}

export const NumberField = memo(function NumberField({
	label,
	value,
	onChange,
	min,
	max,
	step = 0.1,
	inline,
}: NumberFieldProps) {
	const format = useCallback((n: number) => `${+n.toFixed(4)}`, []);

	const clamp = useCallback(
		(n: number) => {
			if (min !== undefined && n < min) return min;
			if (max !== undefined && n > max) return max;
			return n;
		},
		[min, max],
	);

	const [draft, setDraft] = useState(() => format(value));
	const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
	const latestOnChange = useRef(onChange);
	latestOnChange.current = onChange;

	const prevExternal = useRef(value);

	const cancelDebounce = useCallback(() => {
		if (debounceRef.current) {
			clearTimeout(debounceRef.current);
			debounceRef.current = null;
		}
	}, []);

	const flush = useCallback(
		(raw: string) => {
			const n = Number.parseFloat(raw);
			if (Number.isNaN(n)) return;
			const clamped = clamp(n);
			setDraft(format(clamped));
			prevExternal.current = clamped;
			latestOnChange.current(clamped);
		},
		[clamp, format],
	);

	// Sync draft when external value changes (e.g. from another control)
	useEffect(() => {
		if (value !== prevExternal.current) {
			setDraft(format(value));
			prevExternal.current = value;
		}
	}, [value, format]);

	const handleChange = useCallback(
		(e: React.ChangeEvent<HTMLInputElement>) => {
			const raw = e.target.value;
			setDraft(raw);
			cancelDebounce();
			debounceRef.current = setTimeout(() => flush(raw), 150);
		},
		[cancelDebounce, flush],
	);

	const handleBlur = useCallback(() => {
		cancelDebounce();
		flush(draft);
	}, [draft, cancelDebounce, flush]);

	const handleKeyDown = useCallback(
		(e: React.KeyboardEvent<HTMLInputElement>) => {
			if (e.key === "Enter") {
				cancelDebounce();
				flush(draft);
			} else if (e.key === "ArrowUp" || e.key === "ArrowDown") {
				e.preventDefault();
				cancelDebounce();
				const cur = Number.parseFloat(draft);
				if (Number.isNaN(cur)) return;
				const delta = e.key === "ArrowUp" ? step : -step;
				const clamped = clamp(cur + delta);
				setDraft(format(clamped));
				prevExternal.current = clamped;
				latestOnChange.current(clamped);
			}
		},
		[draft, step, cancelDebounce, clamp, flush, format],
	);

	// ── Drag-to-scrub on label ──
	const scrubStart = useRef<{ x: number; val: number } | null>(null);

	const handlePointerDown = useCallback(
		(e: React.PointerEvent<HTMLSpanElement>) => {
			e.preventDefault();
			(e.target as HTMLElement).setPointerCapture(e.pointerId);
			cancelDebounce();
			scrubStart.current = {
				x: e.clientX,
				val: Number.parseFloat(draft) || 0,
			};
		},
		[draft, cancelDebounce],
	);

	const handlePointerMove = useCallback(
		(e: React.PointerEvent<HTMLSpanElement>) => {
			if (!scrubStart.current) return;
			const dx = e.clientX - scrubStart.current.x;
			const newVal = clamp(scrubStart.current.val + dx * step);
			setDraft(format(newVal));
			prevExternal.current = newVal;
			latestOnChange.current(newVal);
		},
		[step, clamp, format],
	);

	const handlePointerUp = useCallback(() => {
		scrubStart.current = null;
	}, []);

	const input = (
		<input
			type="number"
			className="num-input"
			value={draft}
			step={step}
			min={min}
			max={max}
			onChange={handleChange}
			onBlur={handleBlur}
			onKeyDown={handleKeyDown}
		/>
	);

	if (inline) return input;

	return (
		<div className="kv-row">
			<span
				className="kv-key num-label"
				onPointerDown={handlePointerDown}
				onPointerMove={handlePointerMove}
				onPointerUp={handlePointerUp}
			>
				{label}
			</span>
			{input}
		</div>
	);
});

// ── Vec3Field ───────────────────────────────────────────────

interface Vec3FieldProps {
	label: string;
	value: Vec3;
	onChange: (v: Vec3) => void;
	componentLabels?: [string, string, string];
	min?: number;
	max?: number;
	step?: number;
}

export function Vec3Field({
	label,
	value,
	onChange,
	componentLabels = ["x", "y", "z"],
	min,
	max,
	step = 0.1,
}: Vec3FieldProps) {
	const latestOnChange = useRef(onChange);
	latestOnChange.current = onChange;
	const latestValue = useRef(value);
	latestValue.current = value;

	const handleComponent = useCallback(
		(idx: number) => (n: number) => {
			const v = [...latestValue.current] as Vec3;
			v[idx] = n;
			latestOnChange.current(v);
		},
		[],
	);

	return (
		<div className="vec3-row">
			<span className="kv-key" style={{ alignSelf: "center" }}>
				{label}
			</span>
			{([0, 1, 2] as const).map((i) => (
				<div key={componentLabels[i]} className="vec3-cell">
					<span className="vec3-component">{componentLabels[i]}</span>
					<NumberField
						label=""
						value={value[i]}
						onChange={handleComponent(i)}
						min={min}
						max={max}
						step={step}
						inline
					/>
				</div>
			))}
		</div>
	);
}

// ── Toggle ──────────────────────────────────────────────────

interface ToggleProps {
	label: string;
	value: boolean;
	onChange: (v: boolean) => void;
}

export function Toggle({ label, value, onChange }: ToggleProps) {
	return (
		<div className="kv-row">
			<span className="kv-key">{label}</span>
			<button
				type="button"
				className={`toggle-btn ${value ? "active" : ""}`}
				onClick={() => onChange(!value)}
			>
				{value ? "on" : "off"}
			</button>
		</div>
	);
}

// ── MaterialDropdown ────────────────────────────────────────

interface MaterialDropdownProps {
	label: string;
	value: string;
	options: string[];
	onChange: (name: string) => void;
}

export function MaterialDropdown({
	label,
	value,
	options,
	onChange,
}: MaterialDropdownProps) {
	return (
		<div className="kv-row">
			<span className="kv-key">{label}</span>
			<select
				className="mat-select"
				value={value}
				onChange={(e) => onChange(e.target.value)}
			>
				{options.map((name) => (
					<option key={name} value={name}>
						{name}
					</option>
				))}
			</select>
		</div>
	);
}
