#include "integrators/path_tracer.h"

#include "renderer/camera.h"
#include "geometry/hittable.h"
#include "renderer/scene.h"
#include "materials/material.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"

int main()
{
    // World
    hittable_list world;

    auto material_ground = std::make_shared<lambertian>(color(0.8, 0.8, 0.0));
    auto material_center = std::make_shared<lambertian>(color(0.1, 0.2, 0.5));
    auto material_left = std::make_shared<dielectric>(1.50);
    auto material_bubble = std::make_shared<dielectric>(1.00 / 1.50);
    auto material_right = std::make_shared<metal>(color(0.8, 0.6, 0.2), 1.0);
    auto material_red = std::make_shared<lambertian>(color(1.0, 0, 0));

    // world.add(std::make_shared<sphere>(point3(0, 0, -1), 0.5));
    world.add(std::make_shared<sphere>(point3(0, -100.5, -1), 100.0, material_ground));
    world.add(std::make_shared<sphere>(point3(0.0, 0.0, -1.2), 0.5, material_center));
    // world.add(std::make_shared<sphere>(point3(-1.0, 0.0, -1.0), 0.5, material_left));
    world.add(std::make_shared<sphere>(point3(-1.0, 0.0, -1.0), 0.4, material_bubble));
    world.add(std::make_shared<sphere>(point3(1.0, 0.0, -1.0), 0.5, material_right));
    world.add(std::make_shared<triangle>(point3(0, 0.5, -0.8), point3(-0.5, -0.5, -0.5), point3(0.5, -0.5, -0.5), material_red));
    // world.add(std::make_shared<triangle>(point3(-2.0, 2.0, 0.0), point3(-3.0, 0.5, 1.0), point3(-1.0, 0.5, 1.0), material_red));

    // // Wide-angle test
    // auto R = std::cos(pi / 4);
    // auto material_left = std::make_shared<lambertian>(color(0, 0, 1));
    // auto material_right = std::make_shared<lambertian>(color(1, 0, 0));
    // world.add(std::make_shared<sphere>(point3(-R, 0, -1), R, material_left));
    // world.add(std::make_shared<sphere>(point3(R, 0, -1), R, material_right));

    camera cam;

    cam.aspect_ratio = 16.0 / 9.0;
    cam.image_width = 500;
    cam.samples_per_pixel = 100;
    cam.max_depth = 50;

    cam.vfov = 20;
    cam.lookfrom = point3(-4, 4, 2);
    cam.lookat = point3(0, 0, -1);
    cam.vup = vec3(0, 1, 0);

    cam.defocus_angle = 0.6;
    cam.focus_dist = 6.4;

    cam.render(world);

    // /* Cover Render */
    // auto ground_material = std::make_shared<lambertian>(color(0.5, 0.5, 0.5));
    // world.add(std::make_shared<sphere>(point3(0, -1000, 0), 1000, ground_material));

    // for (int a = -11; a < 11; a++)
    // {
    // 	for (int b = -11; b < 11; b++)
    // 	{
    // 		auto choose_mat = random_double();
    // 		point3 center(a + 0.9 * random_double(), 0.2, b + 0.9 * random_double());

    // 		if ((center - point3(4, 0.2, 0)).length() > 0.9)
    // 		{
    // 			std::shared_ptr<material> sphere_material;

    // 			if (choose_mat < 0.8)
    // 			{
    // 				// diffuse
    // 				auto albedo = color::random() * color::random();
    // 				sphere_material = std::make_shared<lambertian>(albedo);
    // 				world.add(std::make_shared<sphere>(center, 0.2, sphere_material));
    // 			}
    // 			else if (choose_mat < 0.95)
    // 			{
    // 				// metal
    // 				auto albedo = color::random(0.5, 1);
    // 				auto fuzz = random_double(0, 0.5);
    // 				sphere_material = std::make_shared<metal>(albedo, fuzz);
    // 				world.add(std::make_shared<sphere>(center, 0.2, sphere_material));
    // 			}
    // 			else
    // 			{
    // 				// glass
    // 				sphere_material = std::make_shared<dielectric>(1.5);
    // 				world.add(std::make_shared<sphere>(center, 0.2, sphere_material));
    // 			}
    // 		}
    // 	}
    // }

    // auto material1 = std::make_shared<dielectric>(1.5);
    // world.add(std::make_shared<sphere>(point3(0, 1, 0), 1.0, material1));

    // auto material2 = std::make_shared<lambertian>(color(0.4, 0.2, 0.1));
    // world.add(std::make_shared<sphere>(point3(-4, 1, 0), 1.0, material2));

    // auto material3 = std::make_shared<metal>(color(0.7, 0.6, 0.5), 0.0);
    // world.add(std::make_shared<sphere>(point3(4, 1, 0), 1.0, material3));

    // camera cam;

    // cam.aspect_ratio = 16.0 / 9.0;
    // cam.image_width = 1200;
    // cam.samples_per_pixel = 500;
    // cam.max_depth = 50;

    // cam.vfov = 20;
    // cam.lookfrom = point3(13, 2, 3);
    // cam.lookat = point3(0, 0, 0);
    // cam.vup = vec3(0, 1, 0);

    // cam.defocus_angle = 0.6;
    // cam.focus_dist = 10.0;

    // cam.render(world);
}
