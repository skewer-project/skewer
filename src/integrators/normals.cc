#include "integrators/normals.h"

#include "core/constants.h"
#include "core/ray.h"
#include "core/spectrum.h"  // Assuming you have a Vec3 or Color class here
#include "core/vec3.h"
#include "film/film.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/surface_interaction.h"
#include "session/render_options.h"

namespace skwr {

void Normals::Render(const Scene &scene, const Camera &cam, Film *film,
                     const IntegratorConfig &config) {
    for (int y = 0; y < film->height(); ++y) {
        for (int x = 0; x < film->width(); ++x) {
            // Integrator calculates normalized coords
            float u = (float)x / film->width();
            float v = (float)y / film->height();
            Ray r = cam.GetRay(u, v);

            SurfaceInteraction si;
            const Float t_min = kShadowEpsilon;
            Spectrum color(0.f);

            if (scene.Intersect(r, t_min, kInfinity, &si)) {
                // If Hit: Visualise Normal
                // Normals range from -1.0 to 1.0.
                // We map them to 0.0 to 1.0 for color display.
                // Color = (Normal + 1) * 0.5
                color = Spectrum((si.n.x() + 1.0f), (si.n.y() + 1.0f), (si.n.z() + 1.0f)) * 0.5f;
            } else {
                // RTIOW blue gradient sky background
                Vec3 unit_direction = Normalize(r.direction());
                auto a = 0.5 * (unit_direction.y() + 1.0);
                color = (1.0 - a) * Spectrum(1.0, 1.0, 1.0) + a * Spectrum(0.5, 0.7, 1.0);
            }
            film->AddSample(x, y, color, 1.0f);
        }
    }
}

}  // namespace skwr
