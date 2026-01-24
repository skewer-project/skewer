#ifndef RAY_H
#define RAY_H

#include "core/vec3.h"

class ray
{
public:
    ray() {}
    ray(const point3 &origin, const vec3 &direction) : orig(origin), dir(direction) {}

    const point3 &origin() const { return orig; }
    const vec3 &direction() const { return dir; }

    // 3D pos (P) on ray is function of P(t) = A + tb, A = origin, b = ray direction
    point3 at(double t) const
    {
        return orig + t * dir;
    }

private:
    point3 orig;
    vec3 dir;
};

#endif