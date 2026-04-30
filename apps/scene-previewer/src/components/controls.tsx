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
	/** If true, disables the drag-to-scrub behavior on the label. */
	noScrub?: boolean;
	/** If true, forces the value to be an integer. */
	integer?: boolean;
	/** If true, the field is read-only. */
	disabled?: boolean;
}

export const NumberField = memo(function NumberField({
	label,
	value,
	onChange,
	min,
	max,
	step = 0.1,
	inline,
	noScrub,
	integer,
	disabled,
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
	const [isEditing, setIsEditing] = useState(false);
	const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
	const latestOnChange = useRef(onChange);

	useEffect(() => {
		latestOnChange.current = onChange;
	}, [onChange]);

	const cancelDebounce = useCallback(() => {
		if (debounceRef.current) {
			clearTimeout(debounceRef.current);
			debounceRef.current = null;
		}
	}, []);

	const flush = useCallback(
		(raw: string) => {
			let n = Number.parseFloat(raw);
			if (Number.isNaN(n)) return;
			if (integer) n = Math.round(n);
			const clamped = clamp(n);
			setDraft(format(clamped));
			setIsEditing(false);
			latestOnChange.current(clamped);
		},
		[clamp, format, integer],
	);

	// Derive display value in render: use draft when editing, otherwise formatted prop value
	const displayValue = isEditing ? draft : format(value);

	const handleFocus = () => {
		setDraft(format(value));
		setIsEditing(true);
	};

	const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
		const raw = e.target.value;
		setDraft(raw);
		setIsEditing(true);
		cancelDebounce();
		debounceRef.current = setTimeout(() => flush(raw), 150);
	};

	const handleBlur = () => {
		cancelDebounce();
		flush(draft);
	};

	const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
		if (e.key === "Enter") {
			cancelDebounce();
			flush(draft);
		} else if (e.key === "ArrowUp" || e.key === "ArrowDown") {
			e.preventDefault();
			cancelDebounce();
			const cur = Number.parseFloat(draft);
			if (Number.isNaN(cur)) return;
			const delta = e.key === "ArrowUp" ? step : -step;
			let clamped = clamp(cur + delta);
			if (integer) clamped = Math.round(clamped);
			setDraft(format(clamped));
			setIsEditing(false);
			latestOnChange.current(clamped);
		}
	};

	// ── Drag-to-scrub on label ──
	const scrubStart = useRef<{ x: number; val: number } | null>(null);

	const handlePointerDown = (e: React.PointerEvent<HTMLSpanElement>) => {
		if (noScrub || disabled) return;
		e.preventDefault();
		(e.target as HTMLElement).setPointerCapture(e.pointerId);
		cancelDebounce();
		setIsEditing(true);
		scrubStart.current = {
			x: e.clientX,
			val: Number.parseFloat(displayValue) || 0,
		};
	};

	const handlePointerMove = (e: React.PointerEvent<HTMLSpanElement>) => {
		if (!scrubStart.current) return;
		const dx = e.clientX - scrubStart.current.x;
		let newVal = clamp(scrubStart.current.val + dx * step);
		if (integer) newVal = Math.round(newVal);
		setDraft(format(newVal));
		setIsEditing(true);
		latestOnChange.current(newVal);
	};

	const handlePointerUp = () => {
		scrubStart.current = null;
	};

	const input = (
		<input
			type="number"
			className="num-input"
			value={displayValue}
			step={step}
			min={min}
			max={max}
			onFocus={handleFocus}
			onChange={handleChange}
			onBlur={handleBlur}
			onKeyDown={handleKeyDown}
			disabled={disabled}
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

// ── Vec2Field ───────────────────────────────────────────────

interface Vec2FieldProps {
	label?: string;
	value: [number, number];
	onChange: (v: [number, number]) => void;
	componentLabels?: [string, string];
	min?: number;
	max?: number;
	step?: number;
	integer?: boolean;
	disabled?: boolean;
}

export const Vec2Field = memo(function Vec2Field({
	label,
	value,
	onChange,
	componentLabels = ["w", "h"],
	min,
	max,
	step = 1,
	integer,
	disabled,
}: Vec2FieldProps) {
	const latestOnChange = useRef(onChange);
	const latestValue = useRef(value);

	useEffect(() => {
		latestOnChange.current = onChange;
	}, [onChange]);

	useEffect(() => {
		latestValue.current = value;
	}, [value]);

	const handleComponent = useCallback(
		(idx: 0 | 1) => (n: number) => {
			const v = [...latestValue.current] as [number, number];
			v[idx] = n;
			latestOnChange.current(v);
		},
		[],
	);

	return (
		<div className="vec2-row">
			<span className="kv-key" style={{ alignSelf: "center" }}>
				{label || ""}
			</span>
			{([0, 1] as const).map((i) => (
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
						integer={integer}
						noScrub={integer}
						disabled={disabled}
					/>
				</div>
			))}
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
	disabled?: boolean;
}

export const Vec3Field = memo(function Vec3Field({
	label,
	value,
	onChange,
	componentLabels = ["x", "y", "z"],
	min,
	max,
	step = 0.1,
	disabled,
}: Vec3FieldProps) {
	const latestOnChange = useRef(onChange);
	const latestValue = useRef(value);

	useEffect(() => {
		latestOnChange.current = onChange;
	}, [onChange]);

	useEffect(() => {
		latestValue.current = value;
	}, [value]);

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
						disabled={disabled}
					/>
				</div>
			))}
		</div>
	);
});

// ── Toggle ──────────────────────────────────────────────────

interface ToggleProps {
	label: string;
	value: boolean;
	onChange: (v: boolean) => void;
}

export const Toggle = memo(function Toggle({
	label,
	value,
	onChange,
}: ToggleProps) {
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
});

// ── Dropdown ────────────────────────────────────────────────

interface DropdownProps {
	label: string;
	value: string;
	options: string[];
	onChange: (name: string) => void;
	/** Optional text for no selection / none */
	noneLabel?: string;
}

export const Dropdown = memo(function Dropdown({
	label,
	value,
	options,
	onChange,
	noneLabel,
}: DropdownProps) {
	return (
		<div className="kv-row">
			<span className="kv-key">{label}</span>
			<select
				className="mat-select"
				value={value}
				onChange={(e) => onChange(e.target.value)}
			>
				{noneLabel !== undefined && <option value="">{noneLabel}</option>}
				{options.map((name) => (
					<option key={name} value={name}>
						{name}
					</option>
				))}
			</select>
		</div>
	);
});

// ── MaterialDropdown ────────────────────────────────────────

interface MaterialDropdownProps {
	label: string;
	value: string;
	options: string[];
	onChange: (name: string) => void;
}

export const MaterialDropdown = memo(function MaterialDropdown({
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
});
