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
#include "io/obj_loader.h"
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
 * Builds the test scene. If obj_file is non-empty, loads it as an object
 * in the scene (replacing the center sphere). Eventually this will be
 * driven by a JSON scene file.
 */
void RenderSession::LoadScene(const std::string& obj_file, const Vec3& obj_scale) {
    std::cout << "[Session] Building scene\n";

    scene_ = std::make_unique<Scene>();

    // A. White Walls (Floor, Ceiling, Back)
    std::cout << "[Session] Building Cornell Box Test...\n";
    Material mat_white;
    mat_white.type = MaterialType::Lambertian;
    mat_white.albedo = Spectrum(0.73f, 0.73f, 0.73f);
    uint32_t id_white = scene_->AddMaterial(mat_white);

    // B. Red Wall (Left)
    Material mat_red;
    mat_red.type = MaterialType::Lambertian;
    mat_red.albedo = Spectrum(0.65f, 0.05f, 0.05f);
    uint32_t id_red = scene_->AddMaterial(mat_red);

    // C. Green Wall (Right)
    Material mat_green;
    mat_green.type = MaterialType::Lambertian;
    mat_green.albedo = Spectrum(0.12f, 0.45f, 0.15f);
    uint32_t id_green = scene_->AddMaterial(mat_green);

    // D. LIGHT
    Material mat_light;
    mat_light.type =
        MaterialType::Lambertian;       // Material type doesn't matter much for pure emitters
    mat_light.albedo = Spectrum(0.0f);  // Black body
    mat_light.emission = Spectrum(4.0f);
    uint32_t id_light = scene_->AddMaterial(mat_light);

    // E. Objects (Glass & Mirror)
    Material mat_glass;
    mat_glass.type = MaterialType::Dielectric;
    mat_glass.ior = 1.5f;
    uint32_t id_glass = scene_->AddMaterial(mat_glass);

    Material mat_mirror;
    mat_mirror.type = MaterialType::Metal;
    mat_mirror.albedo = Spectrum(0.8f);
    mat_mirror.roughness = 0.0f;
    uint32_t id_mirror = scene_->AddMaterial(mat_mirror);

    scene_->AddMesh(CreateQuad(Vec3(5, -5, -5), Vec3(5, -5, -15), Vec3(-5, -5, -15),
                               Vec3(-5, -5, -5), id_white));

    // CEILING
    scene_->AddMesh(
        CreateQuad(Vec3(5, 5, -15), Vec3(5, 5, -5), Vec3(-5, 5, -5), Vec3(-5, 5, -15), id_white));

    // BACK WALL
    scene_->AddMesh(CreateQuad(Vec3(5, -5, -15), Vec3(5, 5, -15), Vec3(-5, 5, -15),
                               Vec3(-5, -5, -15), id_white));

    // LEFT WALL (Red)
    scene_->AddMesh(
        CreateQuad(Vec3(-5, -5, -15), Vec3(-5, 5, -15), Vec3(-5, 5, -5), Vec3(-5, -5, -5), id_red));

    // RIGHT WALL (Green)
    scene_->AddMesh(
        CreateQuad(Vec3(5, -5, -5), Vec3(5, 5, -5), Vec3(5, 5, -15), Vec3(5, -5, -15), id_green));

    // LIGHT
    scene_->AddMesh(CreateQuad(Vec3(2, 4.95, -9), Vec3(-2, 4.95, -9), Vec3(-2, 4.95, -11),
                               Vec3(2, 4.95, -11), id_light));

    // -- Spheres (left and right) --
    scene_->AddSphere(Sphere{Vec3(-2.5f, -3.5f, -12.0f), 1.5f, id_mirror});
    scene_->AddSphere(Sphere{Vec3(2.5f, -3.5f, -8.0f), 1.5f, id_glass});

    // -- Center object: OBJ file or fallback sphere --
    if (!obj_file.empty()) {
        std::cout << "[Session] Loading OBJ: " << obj_file << "\n";
        if (!LoadOBJ(obj_file, *scene_, obj_scale)) {
            std::cerr << "[Session] Failed to load OBJ: " << obj_file << "\n";
        }
    } else {
        std::cout << "no obj file!!!\n";
        Material mat_glass;
        mat_glass.type = MaterialType::Dielectric;
        mat_glass.albedo = Spectrum(1.0f, 1.0f, 1.0f);
        mat_glass.roughness = 0.0f;
        mat_glass.ior = 1.5f;
        uint32_t id_glass = scene_->AddMaterial(mat_glass);
        scene_->AddSphere(Sphere{Vec3(0.0f, -3.5f, -10.0f), 1.5f, id_glass});
    }

    // Build BVH
    scene_->Build();

    // Initialize camera
    float aspect = 16.0f / 9.0f;
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
