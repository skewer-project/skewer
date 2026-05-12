#ifndef SKWR_MATERIALS_TEXTURE_H_
#define SKWR_MATERIALS_TEXTURE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core/color/color.h"

namespace skwr {

constexpr uint32_t kNoTexture = UINT32_MAX;

enum class TextureWrapMode {
    Repeat,
    Clamp,
};

// Image-based texture: stores linear-light RGB float data.
// Sample() performs bilinear interpolation with configurable UV wrapping.
struct ImageTexture {
    std::vector<float> data;  // Linear RGB, w*h*3 floats
    int width = 0;
    int height = 0;

    // Load from file using stb_image (linear float).
    // Returns false on failure.
    bool Load(const std::string& filepath);

    // Sample at UV coordinates with bilinear filtering.
    // Callers pass si.uv.x() and si.uv.y().
    RGB Sample(float u, float v, TextureWrapMode wrap = TextureWrapMode::Repeat) const;

    bool IsValid() const { return !data.empty(); }
};

}  // namespace skwr

#endif  // SKWR_MATERIALS_TEXTURE_H_
