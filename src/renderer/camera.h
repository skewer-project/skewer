#ifndef CAMERA_H
#define CAMERA_H

#include "geometry/hittable.h"
#include "materials/material.h"

class camera
{
public:
    /* Public Camera Parameters Here */
    // we're keeping it simple and having public parameters so the code that
    // uses camera can directly set the parameters. no complicated constructor

    double aspect_ratio = 1.0;  // Ratio image width/height
    int image_width = 100;      // In pixels
    int samples_per_pixel = 10; // Count of random samples for each pixel
    int max_depth = 10;         // Max number of ray bounces into scene

    double vfov = 90;                  // Vertical view angle (field of view)
    point3 lookfrom = point3(0, 0, 0); // Point camera is looking from
    point3 lookat = point3(0, 0, -1);  // Point camera is looking at
    vec3 vup = vec3(0, 1, 0);          // Camera-relative "up" direction

    double defocus_angle = 0; // Variation angle of rays through each pixel
    double focus_dist = 10;   // Distance from camera lookfrom point to plane of perfect focus
    void render(const hittable &world)
    {
        initialize();

        // Render
        std::cout << "P3\n"
                  << image_width << ' ' << image_height << "\n255\n";

        for (int j = 0; j < image_height; ++j)
        {
            std::clog << "\rScanlines remaining: " << (image_height - j) << ' ' << std::flush;
            for (int i = 0; i < image_width; ++i)
            {
                color pixel_color(0, 0, 0);
                for (int sample = 0; sample < samples_per_pixel; ++sample)
                {
                    ray r = get_ray(i, j);
                    pixel_color += ray_color(r, max_depth, world);
                }
                write_color(std::cout, pixel_samples_scale * pixel_color);
            }
        }

        std::clog << "\rDone.		\n";
    }

private:
    /* Private Camera Variables Here */
    int image_height;           // Rendered image height
    double pixel_samples_scale; // Color scale factor for a sum of pixel samples
    point3 camera_center;       // Camera center
    point3 pixel00_loc;         // Location of pixel 0, 0
    vec3 pixel_delta_u;         // Offset to pixel to the right
    vec3 pixel_delta_v;         // Offset to pixel below
    vec3 u, v, w;               // Camera frame basis vectors
    vec3 defocus_disk_u;        // Defocus disk horizontal radius
    vec3 defocus_disk_v;        // Defocus disk vertical radius

    void initialize()
    {
        // Calculate image height, at least 1
        image_height = int(image_width / aspect_ratio);
        image_height = (image_height < 1) ? 1 : image_height;

        pixel_samples_scale = 1.0 / samples_per_pixel;

        camera_center = lookfrom;

        // Camera
        // auto focal_length = (lookfrom - lookat).length();
        auto theta = degrees_to_radians(vfov);
        auto h = std::tan(theta / 2);

        // Viewport calculation (VP is real-valued, so need actual aspec ratio calculated)
        auto viewport_height = 2 * h * focus_dist;
        auto viewport_width = viewport_height * (double(image_width) / image_height);

        // Calculate the u, v, w unit basis vectors for camera coordinate frame
        w = unit_vector(lookfrom - lookat);
        u = unit_vector(cross(vup, w));
        v = cross(w, u);

        // Horizontal/Vertical viewport edge vectors
        auto viewport_u = viewport_width * u;   // horizontal
        auto viewport_v = viewport_height * -v; // vertical (flipped)

        // Horizontal/Vertical delta vectors from pixel to pixel
        pixel_delta_u = viewport_u / image_width;
        pixel_delta_v = viewport_v / image_height;

        // Calculat location of upper-left pixel
        auto viewport_upper_left = camera_center - (focus_dist * w) - viewport_u / 2 - viewport_v / 2;
        pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

        // Calculate camera defocus disk basis vectors
        auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
        defocus_disk_u = u * defocus_radius;
        defocus_disk_v = v * defocus_radius;
    }

    ray get_ray(int i, int j) const
    {
        // Construct a camera ray from defocus disk to a randomly sampled point around pixel loc [i,j]
        auto offset = sample_square(); // returns a ray pointing to a unit square around origin

        // Translating the sample region to pixel loc using the offset x and y
        // We basically inserted the sampling offset to the normal pixel_center calculation:
        //      auto pixel_center = pixel00_loc + (i * pixel_delta_u) + (j * pixel_delta_v);
        //      auto ray_direction = pixel_center - camera_center;
        auto pixel_sample = pixel00_loc + ((i + offset.x()) * pixel_delta_u) + ((j + offset.y()) * pixel_delta_v);
        auto ray_origin = (defocus_angle <= 0) ? camera_center : defocus_disk_sample();
        auto ray_direction = pixel_sample - ray_origin;
        return ray(ray_origin, ray_direction);
    }

    vec3 sample_square() const
    {
        // Returns the vector to a random point in a [-0.5,-0.5]-[+0.5,+0.5] unit square
        return vec3(random_double() - 0.5, random_double() - 0.5, 0);
    }

    point3 defocus_disk_sample() const
    {
        // Returns a random point in the camera defocus disk
        auto p = random_in_unit_disk();
        return camera_center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
    }

    // we're moving ray_color() here
    color ray_color(const ray &r, int depth, const hittable &world) const
    {
        // Base case for ray bounce depth
        if (depth <= 0)
            return color(0, 0, 0);

        hit_record rec;
        if (world.hit(r, interval(0.001, infinity), rec)) // 0.001 for shadow acne (error tolerance point just below surface)
        {
            ray scattered;
            color attenuation;
            if (rec.mat->scatter(r, rec, attenuation, scattered))
                return attenuation * ray_color(scattered, depth - 1, world);
            return color(0, 0, 0);
        }

        vec3 unit_direction = unit_vector(r.direction());
        auto a = 0.5 * (unit_direction.y() + 1.0);                          // want to scale unit_d [-1,1] to [0,1] range
        return (1.0 - a) * color(1.0, 1.0, 1.0) + a * color(0.5, 0.7, 1.0); // linear interpolation white, blue
    }
};

#endif