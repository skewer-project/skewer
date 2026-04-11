# Loom Compositor

Loom is a deep compositor that merges multiple deep EXR layers into a final output.

## The Problem

Deep images store per-pixel opacity as a function of depth. When merging layers (e.g., a character in front of volumetric fog), simple compositing doesn't work - you need to handle overlapping volumes correctly.

## The Solution: Interval Merge Algorithm

Loom uses a 5-step algorithm to correctly merge deep EXRs:

### 1. Boundary Collection

Collect all `z_front` and `z_back` values from both images and sort into unique "events".

### 2. Interval Generation

Create discrete, non-overlapping depth segments from adjacent events.

### 3. Sample Splitting & Alpha Correction

Split samples spanning multiple intervals. Alpha is adjusted using the **Alpha Power Law**:

```
A_new = 1 - (1 - A_orig)^(T_new / T_orig)
```

### 4. Normalization

Sort split samples by `z_front` - now non-overlapping.

### 5. Flattening

Apply standard "Over" operator front-to-back:

```cpp
for (auto& sample : sortedSamples) {
    float remainingVisibility = 1.0f - finalAlpha;
    accumulatedColor += sample.color * remainingVisibility;
    finalAlpha += remainingVisibility * sample.alpha;
}
```

## Architecture

```
Deep EXR Layer 1 ─┐
Deep EXR Layer 2 ─┼─▶ Deep Merging ─▶ Flat EXR/PNG
Deep EXR Layer 3 ─┘
```

## Usage

```bash
./loom --input layer1.exr,layer2.exr --output final.exr
```

| Flag | Description |
|------|-------------|
| `--input` | Comma-separated list of deep EXR paths |
| `--output` | Output path |
| `--width` | Image width |
| `--height` | Image height |
| `--png` | Also output PNG version |

## See Also

- [Architecture Overview](overview.md) - System architecture
- [Skewer](skewer.md) - Renderer with deep EXR output
