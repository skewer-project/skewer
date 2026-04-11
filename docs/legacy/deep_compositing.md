# Deep Compositing: The Interval Merge Algorithm

Deep images differ from flat images because each pixel contains multiple samples with depth ranges. Merging two deep images (e.g., a foreground character and a background volume) is not a simple "A over B" operation; it is an **Interval Merge** problem.

## 1. The Problem: Overlapping Volumes
If Image A contains a volume (like fog from $Z=10$ to $Z=20$) and Image B contains a hard surface (like a window at $Z=15$), the window is physically **inside** the fog. Simple sorting would put the window either entirely in front of or entirely behind the fog, which is visually incorrect.

## 2. The 5-Step Merge Algorithm

### Step 1: Boundary Collection (Interleaving)
For a given pixel, collect all `z_front` and `z_back` values from every sample in both images. Sort these into a unique list of "events."
*   **Example:** Sample A $(10 
ightarrow 20)$, Sample B $(15 
ightarrow 15)$
*   **Events:** $\{10, 15, 20\}$

### Step 2: Interval Generation
Using the sorted "event" list from Step 1, you create a set of discrete, non-overlapping depth segments. Each pair of adjacent events forms a new interval:

-   **Segment 1:** From Z = 10 to Z = 15 (The first part of the fog).
-   **Segment 2:** At Z = 15 (A zero-thickness interval representing the solid object).
-   **Segment 3:** From Z = 15 to Z = 20 (The second part of the fog).

By breaking the depth range into these specific chunks, you ensure that every part of every sample is assigned to exactly one clear "bin" of depth.

### Step 3: Sample Splitting & Alpha Correction
Any sample that spans multiple sub-intervals must be split. When splitting a sample, its **Alpha** must be adjusted to maintain physical consistency.

**The Alpha Power Law:**
If a sample of thickness $T_{orig}$ and opacity $A_{orig}$ is cut into a slice of thickness $T_{new}$, the new opacity $A_{new}$ is:
$$A_{new} = 1 - (1 - A_{orig})^{\frac{T_{new}}{T_{orig}}}$$

*   *Why?* Opacity is exponential. Two 50% opaque layers do not make 100% opacity; they make 75% ($1 - 0.5 	imes 0.5$).

### Step 4: Normalization (Sorting)
Once all samples are split to fit the sub-intervals, no two samples in the list will "overlap" in depth—they will only touch at the boundaries. You can now safely sort the entire master list by `z_front`.

### Step 5: Flattening (The "Over" Operator)
Iterate through the sorted list front-to-back and apply the standard compositing math:

```cpp
for (auto& sample : sortedSamples) {
    float remainingVisibility = 1.0f - finalAlpha;
    accumulatedColor += sample.color * remainingVisibility;
    finalAlpha += remainingVisibility * sample.alpha;
    if (finalAlpha >= 1.0f) break; // Optimization: Early exit
}
```

## 3. High-Performance Implementation: Sweep-line
In a production compositor, creating thousands of temporary `DeepSample` objects is slow. Instead, use a **Sweep-line** approach:
1.  Place all `z_front` and `z_back` events in a priority queue.
2.  Step through events.
3.  Maintain a "Current Active Samples" set.
4.  For each interval between events, calculate the combined contribution of all active samples.

## 4. Summary of Operators
*   **Merge:** Combines two deep pixels into a new deep pixel (keeping all samples, just splitting where necessary).
*   **Flatten:** Turns a deep pixel into a single `(R, G, B, A)` color for final display.
