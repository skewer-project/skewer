#include "integrators/path_tracer.h"

#include "renderer/camera.h"
#include "geometry/hittable.h"
#include "renderer/scene.h"
#include "materials/material.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "io/scene_loader.h"

#include <iostream>
#include <string>

void print_usage(const char* program_name)
{
    std::cerr << "Usage: " << program_name << " <scene.json>\n";
    std::cerr << "       " << program_name << " --demo\n";
    std::cerr << "\n";
    std::cerr << "Options:\n";
    std::cerr << "  <scene.json>  Path to a JSON scene file\n";
    std::cerr << "  --demo        Run with a built-in demo scene\n";
}

void run_demo()
{
    // World
    hittable_list world;

    auto material_ground = make_shared<lambertian>(color(0.8, 0.8, 0.0));
    auto material_center = make_shared<lambertian>(color(0.1, 0.2, 0.5));
    auto material_left = make_shared<dielectric>(1.50);
    auto material_bubble = make_shared<dielectric>(1.00 / 1.50);
    auto material_right = make_shared<metal>(color(0.8, 0.6, 0.2), 1.0);
    auto material_red = make_shared<lambertian>(color(1.0, 0, 0));

    world.add(make_shared<sphere>(point3(0, -100.5, -1), 100.0, material_ground));
    world.add(make_shared<sphere>(point3(0.0, 0.0, -1.2), 0.5, material_center));
    world.add(make_shared<sphere>(point3(-1.0, 0.0, -1.0), 0.4, material_bubble));
    world.add(make_shared<sphere>(point3(1.0, 0.0, -1.0), 0.5, material_right));
    world.add(make_shared<triangle>(point3(0, 0.5, -0.8), point3(-0.5, -0.5, -0.5), point3(0.5, -0.5, -0.5), material_red));

    camera cam;

    cam.aspect_ratio = 16.0 / 9.0;
    cam.image_width = 500;
    cam.samples_per_pixel = 100;
    cam.max_depth = 50;
    cam.background = color(0.70, 0.80, 1.00);  // Sky blue background

    cam.vfov = 20;
    cam.lookfrom = point3(-4, 4, 2);
    cam.lookat = point3(0, 0, -1);
    cam.vup = vec3(0, 1, 0);

    cam.defocus_angle = 0.6;
    cam.focus_dist = 6.4;

    cam.render(world);
}

int main(int argc, char* argv[])
{
    // Check for command line arguments
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    std::string arg = argv[1];

    if (arg == "--demo" || arg == "-d")
    {
        std::clog << "Running demo scene...\n";
        run_demo();
    }
    else if (arg == "--help" || arg == "-h")
    {
        print_usage(argv[0]);
        return 0;
    }
    else
    {
        // Assume it's a scene file path
        std::clog << "Loading scene from: " << arg << "\n";

        try
        {
            scene_data scene = load_scene(arg);
            std::clog << "Scene loaded successfully.\n";
            scene.cam.render(scene.world);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error loading scene: " << e.what() << "\n";
            return 1;
        }
    }

    return 0;
}
