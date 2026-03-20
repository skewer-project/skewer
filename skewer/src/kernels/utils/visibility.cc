#include "kernels/utils/visibility.h"

#include "core/math/constants.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectral_utils.h"
#include "core/spectral/spectrum.h"
#include "kernels/utils/volume_tracking.h"
#include "scene/scene.h"

namespace skwr {

Spectrum EvaluateVisibility(const Scene& scene, Ray& ray, float max_dist, RNG& rng,
                            const SampledWavelengths& wl) {
    Spectrum Tr(1.0f);
    float remaining_dist = max_dist;
    Ray shadow_ray = ray;
    shadow_ray.vol_stack() = ray.vol_stack();

    while (true) {
        SurfaceInteraction shadow_si;
        if (scene.Intersect(shadow_ray, RenderConstants::kRayOffsetEpsilon,
                            remaining_dist - 2.0f * RenderConstants::kRayOffsetEpsilon,
                            &shadow_si)) {
            // Accumulate volume transmittance through the current medium up to the hit
            Tr *= CalculateTransmittance(scene, rng, shadow_ray, shadow_si.t, wl);

            // Transport policy (update if it crosses boundary)
            if (shadow_si.interior_medium != shadow_si.exterior_medium) {
                float to_light_dot_n = Dot(shadow_ray.direction(), shadow_si.n_geom);
                if (to_light_dot_n < 0.0f) {
                    if (shadow_si.interior_medium != kVacuumMediumId &&
                        shadow_si.interior_medium != 0) {
                        shadow_ray.vol_stack().Push(shadow_si.interior_medium, shadow_si.priority);
                    }
                } else {
                    if (shadow_si.interior_medium != kVacuumMediumId &&
                        shadow_si.interior_medium != 0) {
                        shadow_ray.vol_stack().Pop(shadow_si.interior_medium);
                    }
                }
            }

            // Shading policy (check opacity)
            if (shadow_si.material_id != kNullMaterialId) {
                const Material& shadow_mat = scene.GetMaterial(shadow_si.material_id);

                // If it's a solid, opaque object, the light is blocked
                if (shadow_mat.type != MaterialType::Dielectric && !shadow_mat.IsTransparent()) {
                    return Spectrum(0.0f);
                }

                // If it's glass/transparent, attenuate by the surface color
                Tr *= CurveToSpectrum(shadow_mat.albedo, wl);
            }

            // Advance the shadow ray past the surface
            Ray next_ray(
                shadow_si.point + (shadow_ray.direction() * RenderConstants::kRayOffsetEpsilon),
                shadow_ray.direction());
            next_ray.vol_stack() = shadow_ray.vol_stack();
            shadow_ray = next_ray;
            remaining_dist -= shadow_si.t;
            if (remaining_dist <= 0.0f) break;
        } else {
            Tr *= CalculateTransmittance(scene, rng, shadow_ray, remaining_dist, wl);
            break;
        }
    }
    return Tr;
}

}  // namespace skwr
