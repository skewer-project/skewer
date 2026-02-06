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
    Float roughness;  // 0.0 = Perfect Mirror, 1.0 = Matte
    Float ior;        // Index of refraction
};

}  // namespace skwr

#endif  // SKWR_MATERIALS_MATERIAL_H_
