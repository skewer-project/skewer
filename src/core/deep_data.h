#ifndef DEEP_DATA_H
#define DEEP_DATA_H

// #include <ImathColor.h>
#include <vector>
#include "core/vec3.h"

struct DeepSample {
    float depth;
    float alpha;
    vec3 color;

    DeepSample(float d, float a, vec3 c) : 
        depth(d), alpha(a), color(color) {}
};

struct DeepPixel {
    std::vector<DeepSample> samples;
};

struct DeepImage {
    int width;
    int height;

    std::vector<DeepPixel> pixels;

    // Reserves space for a deep image of a given width and height
    DeepImage(int w, int h) : width(w), height(h) {
        pixels.resize(w * h);
    }

    // Get's a single pixel from flat array of pixels using cartesian coordinates 
    // Origin is top left of image
    DeepPixel getPixel(int x, int y) {
        return pixels[y * width + x];
    }
};

#endif