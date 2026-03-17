#ifndef SKWR_SCENE_SKYBOX_H_
#define SKWR_SCENE_SKYBOX_H_

#include "core/math/vec3.h"
#include "core/spectral/spectrum.h"

namespace skwr {

inline Spectrum EvaluateEnvironment(const Vec3& ray_dir, const SampledWavelengths& wl) {
    // STUB: Return a faint, constant ambient light, or pure black.
    // For now, let's return pitch black so it doesn't interfere with our NEE testing.
    (void)ray_dir;
    (void)wl;
    return Spectrum(0.0f);
}

}  // namespace skwr

#endif  // SKWR_SCENE_SKYBOX_H_
