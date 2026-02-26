#ifndef SKWR_MATERIALS_TEXTURE_LOOKUP_H_
#define SKWR_MATERIALS_TEXTURE_LOOKUP_H_

#include <cmath>

#include "core/spectral/spectral_curve.h"
#include "core/spectral/spectral_utils.h"
#include "core/vec3.h"
#include "materials/material.h"
#include "materials/texture.h"
#include "scene/scene.h"
#include "scene/surface_interaction.h"

namespace skwr {

// Resolved per-hit shading data (textures already sampled).
struct ShadingData {
    SpectralCurve albedo;  // Resolved albedo (from texture or flat material color)
    float roughness;       // Resolved roughness (from texture or flat material value)
    Vec3 n_shading;        // Shading normal (may be perturbed by normal map)
};

// Resolve per-hit shading data for the given material and surface interaction.
// Uses si.uv, si.dpdu, si.dpdv for texture lookup and normal-map transform.
inline ShadingData ResolveShadingData(const Material& mat, const SurfaceInteraction& si,
                                      const Scene& scene) {
    ShadingData sd;
    sd.albedo = mat.albedo;
    sd.roughness = mat.roughness;
    sd.n_shading = si.n_geom;

    if (!mat.HasAlbedoTexture() && !mat.HasRoughnessMap() && !mat.HasNormalMap()) {
        return sd;
    }

    float u = si.uv.x();
    float v = si.uv.y();

    // Albedo texture overrides flat material color.
    if (mat.HasAlbedoTexture()) {
        RGB color = scene.GetTexture(mat.albedo_tex).Sample(u, v);
        sd.albedo = RGBToCurve(color);
    }

    // Roughness texture overrides flat roughness value.
    if (mat.HasRoughnessMap()) {
        RGB color = scene.GetTexture(mat.roughness_tex).Sample(u, v);
        sd.roughness = color.r();
    }

    // Normal map: perturb shading normal via TBN transform.
    if (mat.HasNormalMap()) {
        RGB color = scene.GetTexture(mat.normal_tex).Sample(u, v);

        // Convert [0,1] -> [-1,1] tangent-space normal
        Vec3 n_ts(2.0f * color.r() - 1.0f, 2.0f * color.g() - 1.0f, 2.0f * color.b() - 1.0f);

        // Build orthonormal TBN frame.
        // Gram-Schmidt: orthogonalize dpdu w.r.t. n_geom to get T.
        Vec3 N = si.n_geom;
        Vec3 dpdu_proj = si.dpdu - Dot(si.dpdu, N) * N;
        float dpdu_len = dpdu_proj.Length();

        if (dpdu_len > 1e-5f) {
            Vec3 T = dpdu_proj / dpdu_len;
            Vec3 B = Cross(N, T);
            sd.n_shading = Normalize(n_ts.x() * T + n_ts.y() * B + n_ts.z() * N);
        }
        // If dpdu is degenerate, fall back to n_geom (sd.n_shading already set above).
    }

    return sd;
}

}  // namespace skwr

#endif  // SKWR_MATERIALS_TEXTURE_LOOKUP_H_
