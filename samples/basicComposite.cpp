#include <algorithm>
#include <iostream>
#include <vector>

// 1. Define what a "Deep Sample" is
// In OpenEXR, this data sits in the file. Here, we make a struct.
struct DeepSample {
    float depth;  // How far away is it? (Z-depth)
    float red;    // Color Red channel
    float green;  // Color Green channel
    float blue;   // Color Blue channel
    float alpha;  // Opacity (0.0 = invisible, 1.0 = solid)
};

// 2. The Logic: "Flatten" or "Composite" the deep pixel
// This turns the list of samples into one final color.
void compositeDeepPixel(std::vector<DeepSample>& samples) {
    // STEP A: Sort samples by depth (Front to Back)
    // You MUST process the closest object first!
    std::sort(samples.begin(), samples.end(),
              [](const DeepSample& a, const DeepSample& b) { return a.depth < b.depth; });

    // STEP B: The "Over" Operator (Standard Compositing Math)
    float finalR = 0.0f;
    float finalG = 0.0f;
    float finalB = 0.0f;
    float finalAlpha = 0.0f;  // How much light we've blocked so far

    std::cout << "--- Processing Pixel ---" << std::endl;

    for (const auto& sample : samples) {
        // If we are already fully opaque (alpha 1.0), we can't see anything behind!
        if (finalAlpha >= 1.0f) break;

        // Calculate how much visibility is remaining
        float remainingVisibility = 1.0f - finalAlpha;

        // Add the current sample's contribution
        // Math: Current Color * (1 - Alpha of stuff in front of it)
        finalR += sample.red * remainingVisibility;
        finalG += sample.green * remainingVisibility;
        finalB += sample.blue * remainingVisibility;

        // Update the accumulated alpha
        // Math: A_new = A_old + (1 - A_old) * A_current
        finalAlpha += remainingVisibility * sample.alpha;

        std::cout << "Hit object at Depth " << sample.depth << " (Alpha: " << sample.alpha << ")"
                  << " -> Accumulated Alpha is now: " << finalAlpha << std::endl;
    }

    std::cout << "FINAL PIXEL COLOR: (" << finalR << ", " << finalG << ", " << finalB << ")"
              << std::endl;
}

int main() {
    // 3. Create a Deep Pixel (A list of samples)
    std::vector<DeepSample> myPixel;

    // Add a Blue Wall far away (Depth 10, Solid)
    myPixel.push_back({10.0f, 0.0f, 0.0f, 1.0f, 1.0f});

    // Add Red Fog closer to camera (Depth 5, 50% opacity)
    myPixel.push_back({5.0f, 1.0f, 0.0f, 0.0f, 0.5f});

    // Add Green Glass even closer (Depth 2, 20% opacity)
    myPixel.push_back({2.0f, 0.0f, 1.0f, 0.0f, 0.2f});

    // 4. Run the Flattening
    compositeDeepPixel(myPixel);

    return 0;
}