#ifndef SKWR_KERNELS_UTILS_DIRECT_LIGHTING_H_
#define SKWR_KERNELS_UTILS_DIRECT_LIGHTING_H_

#include "core/math/vec3.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectral_utils.h"
#include "core/spectral/spectrum.h"
#include "scene/scene.h"

namespace skwr {

struct DirectLightSample {
    Vec3 wi;            // Direction TO the light
    float dist;         // Distance to the light
    float pdf;          // Combined PDF (light selection + solid angle)
    Spectrum emission;  // Unattenuated light emission
};

inline bool GenerateLightSample(const Vec3& origin, const Scene& scene, RNG& rng,
                                const SampledWavelengths& wl, DirectLightSample* out_sample) {
    if (scene.Lights().empty()) return false;

    int light_index = int(rng.UniformFloat() * scene.Lights().size());
    LightSample ls = SampleLight(scene, light_index, rng);

    Vec3 to_light = ls.p - origin;
    float dist_sq = to_light.LengthSquared();
    if (dist_sq <= 0.0f) return false;
    out_sample->dist = std::sqrt(dist_sq);
    out_sample->wi = to_light / out_sample->dist;

    // Area PDF -> Solid Angle PDF: PDF_w = PDF_a * dist^2 / cos_light
    float cos_light = std::fmax(0.0f, Dot(-out_sample->wi, ls.n));
    if (cos_light <= 0.0f) return false;
    float light_pdf_w = ls.pdf * dist_sq / cos_light;

    // Weight = 1.0 / (N_lights * PDF_w)
    out_sample->pdf = light_pdf_w * scene.InvLightCount();
    out_sample->emission = CurveToSpectrum(ls.emission, wl);

    return true;
}

}  // namespace skwr

#endif  // SKWR_KERNELS_UTILS_DIRECT_LIGHTING_H_
