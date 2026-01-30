#include "film/film.h"
#include "film/image_buffer.h"

namespace skwr
{

    Film::Film(int width, int height) : width_(width), height_(height) {}

    void Film::AddSample(int x, int y, const Spectrum &L, float weight = 1.0f)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return;

        int index = y * width_ + x; // row-major order

        // Simple Box Filter Accumulation
        pixels_[index].color_sum += L * weight;
        // pixels_[index].alpha_sum += alpha * weight;
        pixels_[index].weight_sum += weight;
    }

    void Film::AddDeepSample(int x, int y, float depth, const Spectrum &L, float transmittance) {}

    void Film::WriteImage(const std::string &filename) const
    {
        // Create a TEMPORARY buffer just for this export
        ImageBuffer temp_buffer(width_, height_);

        // Bake the data (Convert Accumulator -> Output Format)
        for (int y = 0; y < height_; ++y)
        {
            for (int x = 0; x < width_; ++x)
            {
                int index = y * width_ + x;
                const Pixel &p = pixels_[index];

                Spectrum final_color(0, 0, 0);
                if (p.weight_sum > 0)
                {
                    final_color = p.color_sum / p.weight_sum;
                }

                temp_buffer.SetPixel(x, y, final_color);
            }
        }

        // Save to disk
        temp_buffer.WritePPM(filename);
    }
}