#ifndef SKWR_FILM_FILM_H_
#define SKWR_FILM_FILM_H_

#include <vector>

#include "core/spectrum.h"
#include "film/image_buffer.h"

namespace skwr {

// These structs will likely be moved to their own headers later.
// Here initially for simplicity

struct Pixel {
    // TODO: RGB and alpha. should be able to accumulate color like RTIOW does it
    Spectrum color_sum = Spectrum();  // Accumulated Radiance
    float alpha_sum = 0.f;            // Accumulated Opacity
    float weight_sum = 0.f;           // Total weight (filter weight * count)

    // // Helper to get the final averaged value
    // Spectrum GetAverageColor() const
    // {
    //     if (weight_sum == 0)
    //         return Spectrum();
    //     return color_sum / weight_sum;

    //     // we need weight_sum instead of something like int sample_count
    //     // Anti-Aliasing: In high-quality rendering, a ray that hits the center of a pixel
    //     // is worth more (1.0) than a ray that hits the edge (0.1).
    //     // This is called "Pixel Filtering" (Gaussian or Mitchell-Netravali filters).
    //     // FinalColor= ∑(SampleColor×FilterWeight)/∑FilterWeight
    // }
};

class Film {
  public:
    Film(int width, int height);

    // Standard Image: Adds color to a pixel (thread-safe!)
    void AddSample(int x, int y, const Spectrum& L, float weight);

    // TODO: Deep sampling after standard pixels are supported
    void AddDeepSample(int x, int y, float depth, const Spectrum& L, float transmittance);

    // Saves to disk (PPM, EXR)
    void WriteImage(const std::string& filename) const;

    int width() { return width_; }
    int height() { return height_; }

  private:
    int width_, height_;
    std::vector<Pixel> pixels_;           // The flat image
    std::vector<DeepPixel> deep_pixels_;  // The deep image buffer
};

}  // namespace skwr

#endif  // SKWR_FILM_FILM_H_
