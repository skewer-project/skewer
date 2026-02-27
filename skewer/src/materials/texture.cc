// STB_IMAGE_IMPLEMENTATION must be defined in exactly one .cc file.
// (Removed from io/rtw_stb_image.h to avoid duplicate symbol issues.)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include "materials/texture.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "stb_image.h"

namespace skwr {

bool ImageTexture::Load(const std::string& filepath) {
    int n;
    float* raw = stbi_loadf(filepath.c_str(), &width, &height, &n, 3);
    if (!raw) {
        std::cerr << "[Texture] Failed to load: " << filepath << " (" << stbi_failure_reason()
                  << ")\n";
        width = 0;
        height = 0;
        return false;
    }
    data.assign(raw, raw + width * height * 3);
    stbi_image_free(raw);
    std::clog << "[Texture] Loaded: " << filepath << " (" << width << "x" << height << ")\n";
    return true;
}

RGB ImageTexture::Sample(float u, float v) const {
    if (data.empty()) return RGB(1.0f, 0.0f, 1.0f);  // Magenta = missing texture

    // Repeat (tiling) wrapping
    u = u - std::floor(u);
    v = v - std::floor(v);

    // Bilinear interpolation
    float fx = u * static_cast<float>(width - 1);
    float fy = v * static_cast<float>(height - 1);

    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, width - 1);
    int y1 = std::min(y0 + 1, height - 1);

    float tx = fx - static_cast<float>(x0);
    float ty = fy - static_cast<float>(y0);

    auto fetch = [&](int x, int y) -> RGB {
        int idx = (y * width + x) * 3;
        return RGB(data[idx], data[idx + 1], data[idx + 2]);
    };

    RGB c00 = fetch(x0, y0);
    RGB c10 = fetch(x1, y0);
    RGB c01 = fetch(x0, y1);
    RGB c11 = fetch(x1, y1);

    // Bilinear blend
    RGB r0 = (1.0f - tx) * c00 + tx * c10;
    RGB r1 = (1.0f - tx) * c01 + tx * c11;
    return (1.0f - ty) * r0 + ty * r1;
}

}  // namespace skwr
