#include "film/image_buffer.h"

#include <fstream>
#include <iostream>
#include <ostream>

#include "core/spectrum.h"

namespace skwr {

ImageBuffer::ImageBuffer(int width, int height) : width_(width), height_(height) {
    pixels_.resize(width * height);
}

void ImageBuffer::SetPixel(int x, int y, const Spectrum &s) {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
    pixels_[y * width_ + x] = s;
}

// For debug and testing purposes, we can keep this PPM writer but
// ultimately we should move this to a separate src/io/image_io.h or something
void ImageBuffer::WritePPM(const std::string &filename) const {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Error: Could not open " << filename << " for writing.\n";
        return;
    }

    // PPM Header: P3 = ASCII RGB, then width, height, max_val
    out << "P3\n" << width_ << " " << height_ << "\n255\n";

    for (const auto &pixel : pixels_) {
        Color c = pixel.ToColor();
        c.ApplyGammaCorrection();
        c.Clamp(0.0f, 1.0f);
        // Convert float (0.0-1.0) to int (0-255)
        int ir = static_cast<int>(255.999 * c.r());
        int ig = static_cast<int>(255.999 * c.g());
        int ib = static_cast<int>(255.999 * c.b());

        out << ir << " " << ig << " " << ib << "\n";
    }

    out.close();
    std::cout << "Wrote image to " << filename << "\n";
}

}  // namespace skwr
