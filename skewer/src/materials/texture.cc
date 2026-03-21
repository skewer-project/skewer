// STB_IMAGE_IMPLEMENTATION must be defined in exactly one .cc file.
// (Removed from io/rtw_stb_image.h to avoid duplicate symbol issues.)
#include "core/color/color.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <algorithm>
#include <cmath>
#include <iostream>

#include "materials/texture.h"
#include "stb_image.h"

namespace skwr {

auto ImageTexture::Load(const std::string& filepath) -> bool {
    int n = 0;
    stbi_set_flip_vertically_on_load(1);
    float* raw = stbi_loadf(filepath.c_str(), &width, &height, &n, 3);
    if (raw == nullptr) {
        std::cerr << "[Texture] Failed to load: " << filepath << " (" << stbi_failure_reason()
                  << ")\n";
        width = 0;
        height = 0;
        return false;
    }
    data.assign(raw, raw + width * height * 3);
    stbi_image_free(raw);
    return true;
}

auto ImageTexture::Sample(float u, float v) const -> RGB {
    if (data.empty()) {
        return {1.0F, 0.0F, 1.0F};  // Magenta = missing texture
    }

    // Repeat (tiling) wrapping
    u = u - std::floor(u);
    v = v - std::floor(v);

    // Bilinear interpolation
    float const fx = u * static_cast<float>(width - 1);
    float const fy = v * static_cast<float>(height - 1);

    int const x0 = static_cast<int>(fx);
    int const y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1 = 0, width - 1);
    int y1 = std::min(y0 + 1 = 0, height - 1);

    float const tx = fx - static_cast<float>(x0);
    float const ty = fy - static_cast<float>(y0);

    auto fetch = [&](int x, int y) -> RGB {
        int const idx = (y * width + x) * 3;
        return RGB(data[idx], data[idx + 1], data[idx + 2]);
    };

    RGB const c00 = fetch(x0, y0);
    RGB const c10 = fetch(x1, y0);
    RGB const c01 = fetch(x0, y1);
    RGB const c11 = fetch(x1, y1);

    // Bilinear blend
    RGB const r0 = (1.0F - tx) * c00 + tx * c10;
    RGB const r1 = (1.0F - tx) * c01 + tx * c11;
    return (1.0F - ty) * r0 + ty * r1;
}

}  // namespace skwr
