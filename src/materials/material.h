#ifndef SKWR_MATERIALS_MATERIAL_H_
#define SKWR_MATERIALS_MATERIAL_H_

#include <cstdint>

#include "core/spectrum.h"

namespace skwr {

enum class MaterialType : uint8_t { Lambertian, Metal, Dielectric };

// 32-byte aligned to fit in cache?
struct alignas(16) Material {
    Spectrum albedo;                    // Color (Diffuse or Specular)
    Spectrum emission;                  //
    float roughness;                    // 0.0 = Perfect Mirror, 1.0 = Matte
    float ior;                          // Index of refraction
    Spectrum opacity = Spectrum(1.0f);  // 1 = opaque, 0 = fully transparent
    // OR: texture reference later
    MaterialType type;

    bool IsEmissive() const { return emission.MaxComponent() > 0.0f; }
    bool IsTransparent() const { return opacity.MinComponent() < 1.0f; }
};

}  // namespace skwr

#endif  // SKWR_MATERIALS_MATERIAL_H_
