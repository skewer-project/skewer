#ifndef SPECTRUM_H
#define SPECTRUM_H

#include "core/interval.h"
#include "core/vec3.h"

// TODO: refactor to handle color physics separate from geometry

using color = vec3;

// accounting for gamma correction in our linear images
inline double linear_to_gamma(double linear_component) {
    if (linear_component > 0) return std::sqrt(linear_component);
    return 0;
}

void write_color(std::ostream& out, const color& pixel_color) {
    auto r = pixel_color.x();
    auto g = pixel_color.y();
    auto b = pixel_color.z();

    // Apply a linear to gamma transform for gamma 2 ^(1/gamma) = sqrt
    r = linear_to_gamma(r);
    g = linear_to_gamma(g);
    b = linear_to_gamma(b);

    // Translate [0,1] component values to rgb range [0,255]
    static const interval intensity(0.000, 0.999);
    int rbyte = int(256 * intensity.clamp(r));
    int gbyte = int(256 * intensity.clamp(g));
    int bbyte = int(256 * intensity.clamp(b));

    // since adding average of all samples, need to clamp values to prevent going
    // beyond [0,1] range

    // Write out pixel components
    out << rbyte << ' ' << gbyte << ' ' << bbyte << '\n';
}

#endif