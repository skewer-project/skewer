#pragma once

#include "deep_image.h"

namespace exrio {

inline DeepSample makePoint(float z, float r, float g, float b, float a) {
    return DeepSample(z, r, g, b, a);
}

inline DeepSample makeVolume(float zFront, float zBack, float r, float g, float b, float a) {
    return DeepSample(zFront, zBack, r, g, b, a);
}

inline DeepImage makeImage1x1(float z, float r, float g, float b, float a) {
    DeepImage img(1, 1);
    img.pixel(0, 0).addSample(makePoint(z, r, g, b, a));
    return img;
}

inline DeepImage makeVolumeImage1x1(float zFront, float zBack, float r, float g, float b, float a) {
    DeepImage img(1, 1);
    img.pixel(0, 0).addSample(makeVolume(zFront, zBack, r, g, b, a));
    return img;
}

}  // namespace exrio
