# Interval Merge Algorithm

Loom's primary responsibility is merging deep data from multiple independent layers into a single, physically correct deep image. This is accomplished using the **Interval Merge Algorithm** (`loom/src/deep_merger.cc`).

## The Core Problem

Unlike flat images where an "Over" operator ($A \text{ over } B$) is sufficient, deep images contain volumetric samples that occupy a range of depths. If Layer A contains fog from $Z=10$ to $Z=50$, and Layer B contains a character at $Z=30$, the character must be "embedded" within the fog.

## The 5-Step Merge Process

To solve this, Loom follows five distinct stages for every pixel:

1. **Boundary Collection**: Gather all `z_front` and `z_back` values from every sample in every layer.
2. **Interval Splitting**: Every volumetric sample that spans across multiple boundaries is split into smaller "fragments" at each boundary point.
3. **Alpha Correction**: When a sample is split, the alpha of the new fragments is calculated using the **Beer-Lambert Power Law** to maintain physical consistency:
   $\alpha_{fragment} = 1 - (1 - \alpha_{original})^{\frac{\text{thickness}_{fragment}}{\text{thickness}_{original}}}$
4. **Uniform Interspersion**: Fragments that now share the exact same $[Z_{front}, Z_{back}]$ interval are merged using the "Over" operator:
   $\alpha_{combined} = \alpha_a + \alpha_b - \alpha_a \cdot \alpha_b$
   $C_{combined} = (C_a + C_b) \cdot \frac{\alpha_{combined}}{\alpha_a + \alpha_b}$
5. **Depth Sorting**: The final, blended fragments are sorted by depth to produce the final Deep EXR output.

## Sample Splitting Rationale

We split samples into intervals because it allows us to treat every segment as a discrete "slab" of volume. Once everything is split into the same intervals, the complex 3D merging problem reduces to a 2D compositing problem within each interval, which can be solved with standard blending operators.
