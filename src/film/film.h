#ifndef SKWR_FILM_FILM_H_
#define SKWR_FILM_FILM_H_

#include <vector>
#include "core/spectrum.h"

namespace skwr
{

    struct DeepPixel
    {
        // TODO: Add list of samples {depth, color, transmittance} here
    };

    class Film
    {
    public:
        Film(int width, int height);

        // Standard Image: Adds color to a pixel (thread-safe!)
        void AddSample(int x, int y, const Spectrum &L, float weight = 1.0f);

        // Deep Image: Records a sample at a specific depth
        // TODO: Implement this for your "Phase 3"
        void AddDeepSample(int x, int y, float depth, const Spectrum &L, float transmittance);

        // Saves to disk (PPM, EXR)
        void WriteImage(const std::string &filename) const;

    private:
        int width_, height_;
        std::vector<Spectrum> pixels_;       // The flat image
        std::vector<DeepPixel> deep_pixels_; // The deep image buffer
    };

} // namespace skwr

#endif // SKWR_FILM_FILM_H_