#ifndef SKWR_MATERIALS_MATERIAL_H_
#define SKWR_MATERIALS_MATERIAL_H_

#include <cstdint>

#include "core/spectral/spectral_curve.h"

namespace skwr {

enum class MaterialType : uint8_t { Lambertian, Metal, Dielectric };

// 32-byte aligned to fit in cache?
struct alignas(16) Material {
    SpectralCurve albedo;                        // Color (Diffuse or Specular)
    SpectralCurve emission;                      //
    float roughness;                             // 0.0 = Perfect Mirror, 1.0 = Matte
    float ior;                                   // Index of refraction
    float dispersion;                            // Cauchy b coeff (a is ior)
    SpectralCurve opacity = {{1.0f, 1.0f, 1.0f}};  // 1 = opaque, 0 = fully transparent
    // OR: texture reference later
    MaterialType type;

    bool IsEmissive() const { return emission.scale > 0.0f; }
    bool IsTransparent() const {
        return opacity.coeff[0] < 1.0f || opacity.coeff[1] < 1.0f || opacity.coeff[2] < 1.0f;
    }
};

}  // namespace skwr

#endif  // SKWR_MATERIALS_MATERIAL_H_
