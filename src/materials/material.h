#ifndef SKWR_MATERIALS_MATERIAL_H_
#define SKWR_MATERIALS_MATERIAL_H_

#include <cstdint>

#include "core/spectrum.h"

namespace skwr {

enum class MaterialType : uint8_t { Lambertian, Metal, Dielectric };

// 32-byte aligned to fit in cache?
struct Material {
    MaterialType type;

    // padding to align data types
    uint8_t _padding[3];

    // Data params
    Spectrum albedo;  // Color (Diffuse or Specular)
    Spectrum emission;
    float roughness;  // 0.0 = Perfect Mirror, 1.0 = Matte
    float ior;        // Index of refraction

    bool IsEmissive() const { return emission.r() > 0 || emission.g() > 0 || emission.b() > 0; }
};

}  // namespace skwr

#endif  // SKWR_MATERIALS_MATERIAL_H_
