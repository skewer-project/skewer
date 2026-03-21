#include "integrators/normals.h"

#include "core/color/color.h"
#include "core/math/constants.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/transport/surface_interaction.h"
#include "film/film.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "session/render_options.h"

namespace skwr {

void Normals::Render(const Scene& scene, const Camera& cam, Film* film,
                     const IntegratorConfig& config) {
    (void)config;
    for (int y = 0; y < film->height(); ++y) {
        for (int x = 0; x < film->width(); ++x) {
            // Integrator calculates normalized coords
            float const u = static_cast<float>(x) / film->width();
            float const v = static_cast<float>(y) / film->height();
            RNG rng;
            Ray const r = cam.GetRay(u, v, rng);

            SurfaceInteraction si;
            const float kTMin = RenderConstants::kRayOffsetEpsilon;
            RGB color(0.F);

            if (scene.Intersect(r, kTMin, MathConstants::kFloatInfinity, &si)) {
                // If Hit: Visualise Normal
                // Normals range from -1.0 to 1.0.
                // We map them to 0.0 to 1.0 for color display.
                // Color = (Normal + 1) * 0.5
                color =
                    RGB((si.n_geom.x() + 1.0F), (si.n_geom.y() + 1.0F), (si.n_geom.z() + 1.0F)) *
                    0.5F;
            } else {
                // RTIOW blue gradient sky background
                Vec3 const unit_direction = Normalize(r.direction());
                auto a = 0.5 * (unit_direction.y() + 1.0);
                color = (1.0 - a) * RGB(1.0, 1.0, 1.0) + a * RGB(0.5, 0.7, 1.0);
            }
            film->AddSample(x, y, color, 1.0F, 1.0F);
        }
    }
}

}  // namespace skwr
