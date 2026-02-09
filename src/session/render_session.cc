#include "session/render_session.h"

#include <cstdint>
#include <iostream>
#include <memory>

#include "core/spectrum.h"
#include "core/vec3.h"
#include "film/film.h"
#include "geometry/sphere.h"
#include "integrators/integrator.h"
#include "integrators/normals.h"
#include "integrators/path_trace.h"
#include "materials/material.h"
#include "scene/camera.h"
#include "scene/mesh_utils.h"
#include "scene/scene.h"
#include "session/render_options.h"

namespace skwr {

/* FACTORY FUNCTION for Creating Integrators */
static std::unique_ptr<Integrator> CreateIntegrator(IntegratorType type) {
    switch (type) {
        case IntegratorType::PathTrace:
            return std::make_unique<PathTrace>();
        case IntegratorType::Normals:
            return std::make_unique<Normals>();
        default:
            return nullptr;
    }
}

// Initialize pointers to nullptr or default states
// Pointers default to nullptr implicitly, but explicit is fine
RenderSession::RenderSession() {}
RenderSession::~RenderSession() = default;  // Unique_ptr handles cleanup automatically

/**
 * Loads file into scene format
 */
void RenderSession::LoadScene(const std::string& filename) {
    std::cout << "[Session] Loading Scene: " << filename << " (STUB)\n";

    scene_ = std::make_unique<Scene>();

    // Temporarily hardcoding sphere into scene...
    // TODO: Reconnect tinyobjloader and asset loading
    Material mat_ground;
    mat_ground.type = MaterialType::Lambertian;
    mat_ground.albedo = Spectrum(0.8f, 0.8f, 0.0f);
    mat_ground.roughness = 1.0f;  // Ignored for Lambertian usually
    uint32_t id_ground = scene_->AddMaterial(mat_ground);

    Material mat_glass;
    mat_glass.type = MaterialType::Dielectric;
    mat_glass.albedo = Spectrum(1.0f, 1.0f, 1.0f);  // Glass doesn't absorb light
    mat_glass.roughness = 0.0f;
    mat_glass.ior = 1.5f;  // Standard Glass
    uint32_t id_glass = scene_->AddMaterial(mat_glass);

    Material metal;
    metal.type = MaterialType::Metal;
    metal.roughness = 0.0;
    metal.albedo = Spectrum(0.8f);
    uint32_t metal_id = scene_->AddMaterial(metal);
    scene_->AddSphere(Sphere{Vec3(-2.1f, 0.0f, -3.0f), 1.0f, metal_id});

    Material mat_red;
    mat_red.type = MaterialType::Lambertian;
    mat_red.albedo = Spectrum(0.7f, 0.3f, 0.3f);
    uint32_t id_red = scene_->AddMaterial(mat_red);

    // scene_->AddSphere(Sphere{Vec3(0.0f, -1001.0f, -1.0f), 1000.0f, id_ground});
    scene_->AddSphere(Sphere{Vec3(0.0f, 0.0f, -3.0f), 1.0f, id_glass});
    scene_->AddSphere(Sphere{Vec3(2.1f, 0.0f, -3.0f), 1.0f, id_red});
    // NEW: Floor Quad
    // Let's make it 20x20 units, centered at Y=-1.0
    float size = 10.0f;
    float y_floor = -1.0f;

    // Define corners (Counter-Clockwise order looking from top)
    Vec3 p0(-size, y_floor, size);   // Front-Left
    Vec3 p1(size, y_floor, size);    // Front-Right
    Vec3 p2(size, y_floor, -size);   // Back-Right
    Vec3 p3(-size, y_floor, -size);  // Back-Left

    // Create and Upload
    scene_->AddMesh(CreateQuad(p0, p1, p2, p3, id_ground));

    // Build BVH
    scene_->Build();

    // Initilize camera
    // Looking from (0, 0, 0) to (0, 0, -1)
    // Should these be a part of RenderOptions? Maybe refactor camera first...
    Float aspect = 16.0f / 9.0f;
    camera_ = std::make_unique<Camera>(Vec3(0.0f, 1.0f, 1.5f),   // LookFrom
                                       Vec3(0.0f, 0.0f, -3.0f),  // LookAt
                                       Vec3(0.0f, 1.0f, 0.0f), 90.0f, aspect);
}

/**
 * Set user options
 */
void RenderSession::SetOptions(const RenderOptions& options) {
    options_ = options;
    film_ = std::make_unique<Film>(options_.image_config.width, options_.image_config.height);
    integrator_ = CreateIntegrator(options_.integrator_type);

    std::cout << "[Session] Options Set: " << options_.image_config.width << "x"
              << options_.image_config.height
              << " | Samples: " << options_.integrator_config.samples_per_pixel << "\n";
}

/**
 * Call integrator render loop
 */
void RenderSession::Render() {
    if (!film_ || !integrator_ || !scene_) {
        std::cerr << "[Error] Session not ready. Missing Film, Integrator, or Scene.\n";
        return;
    }

    std::cout << "[Session] Starting Render...\n";

    integrator_->Render(*scene_, *camera_, film_.get(), options_.integrator_config);
    // .get() extracts the raw pointer held inside. Integrator needs to write pixels to film, so
    // it's mutable, but we don't want to transfer ownership

    std::cout << "[Session] Render Complete.\n";
}

/**
 * Convert film to image or deep buffer
 */
void RenderSession::Save() const {
    if (film_) {
        film_->WriteImage(options_.image_config.outfile);
    }
}

}  // namespace skwr
