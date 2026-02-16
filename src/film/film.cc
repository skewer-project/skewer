#include "film/film.h"

#include "core/spectrum.h"
#include "film/image_buffer.h"

namespace skwr {

Film::Film(int width, int height) : width_(width), height_(height) {
    pixels_.resize(width_ * height_);
}

void Film::AddSample(int x, int y, const Spectrum&  /*l*/, float weight) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) { return;
}

    int const index = (y * width_) + x;  // row-major order

    // Simple Box Filter Accumulation
    pixels_[index].color_sum += L * weight;
    // pixels_[index].alpha_sum += alpha * weight;
    pixels_[index].weight_sum += weight;
}

void Film::AddDeepSample(int x, int y, float depth, const Spectrum& l, float transmittance) {}

void Film::WriteImage(const std::string& filename) const {
    // Create a TEMPORARY buffer just for this export
    ImageBuffer const temp_buffer(width_, height_);

    // Bake the data (Convert Accumulator -> Output Format)
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            int const index = (y * width_) + x;
            const Pixel& p = pixels_[index];

            Spectrum final_color(0, 0, 0);
            if (p.weight_sum > 0) {
                final_color = p.color_sum / p.weight_sum;
            }

            temp_buffer.SetPixel(x, y, final_color);
        }
    }

    // Save to disk
    temp_buffer.WritePPM(filename);
}
}  // namespace skwr
