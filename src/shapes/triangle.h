#ifndef TRIANGLE_H
#define TRIANGLE_H

#include "geometry/hittable.h"

class triangle : public hittable
{
public:
    triangle(const point3 &v0, const point3 &v1, const point3 &v2, std::shared_ptr<material> mat) : v0(v0), v1(v1), v2(v2), mat(mat)
    {
        // Equation of plane is Ax + By + Cz + D = 0
        // Where A, B, C are the components of the normal N
        // and x, y, z are the coords of any point on the plane
        // D is offset.

        // Need normal of plane the triangle sits on
        vec3 v0v1 = v1 - v0; // might need to flip for culling?
        vec3 v0v2 = v2 - v0;
        N = unit_vector(cross(v0v1, v0v2)); // N = plane normal
        D = -dot(N, v0);
    }

    bool hit(const ray &r, interval ray_t, hit_record &rec) const override
    {
        // Calculate P and t
        double denom = dot(N, r.direction());

        if (std::fabs(denom) < 1e-8)
        {
            return false;
        }

        // Check interval of t
        double t = -(dot(N, r.origin()) + D) / denom;
        if (!ray_t.surrounds(t))
            return false;

        point3 p = r.at(t);

        // Inside-outside test
        vec3 v0v1 = v1 - v0;
        vec3 v1v2 = v2 - v1;
        vec3 v2v0 = v0 - v2;

        // Test edge v0 -> v1
        vec3 v0p = p - v0;
        if (dot(N, cross(v0v1, v0p)) < 0)
            return false; // point not in tri

        // Test edge v1 -> v2
        vec3 v1p = p - v1;
        if (dot(N, cross(v1v2, v1p)) < 0)
            return false;

        // Test edge v2 -> v0
        vec3 v2p = p - v2;
        if (dot(N, cross(v2v0, v2p)) < 0)
            return false;

        // Write to hit record after all checks pass
        rec.t = t;
        rec.p = p;
        rec.set_face_normal(r, N);
        rec.mat = mat;

        return true;
    }

private:
    point3 v0, v1, v2; // Gotta assume counter clockwise winding
    vec3 N;
    double D;
    std::shared_ptr<material> mat;
};

#endif