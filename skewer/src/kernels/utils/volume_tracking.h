#ifndef SKWR_KERNELS_UTILS_VOLUME_TRACKING_H_
#define SKWR_KERNELS_UTILS_VOLUME_TRACKING_H_

#include "core/math/vec3.h"
#include "core/spectral/spectrum.h"

namespace skwr {

struct GridMedium;
struct NanoVDBMedium;
class Scene;
class Ray;
class RNG;

Spectrum CalculateGridTransmittance(const GridMedium& medium, RNG& rng, const Ray& shadow_ray,
                                    float dist);
Spectrum CalculateTransmittance(const Scene& scene, RNG& rng, const Ray& shadow_ray, float dist,
                                const SampledWavelengths& wl);
Spectrum CalculateNanoVDBTransmittance(const NanoVDBMedium& medium, RNG& rng, const Ray& shadow_ray,
                                       float dist, const SampledWavelengths& wl);
float EvalHenyeyGreenstein(float g, const Vec3& wo, const Vec3& wi);
void SampleHenyeyGreenstein(float g, const Vec3& wo, float u1, float u2, Vec3& wi);

}  // namespace skwr

#endif  // SKWR_KERNELS_UTILS_VOLUME_TRACKING_H_
