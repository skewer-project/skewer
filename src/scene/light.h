#pragma once
#include <cstdint>

#include "core/spectrum.h"

// THIS IS JUST A TEMP FILE WHILE TESTING!!

namespace skwr {
// A "Light" is just a reference to a primitive that emits energy.
// We use this for Importance Sampling (picking a light to shoot at).
struct AreaLight {
    // 1. What is it?
    // We can use a union or variant if we have multiple types
    enum Type { Sphere, Triangle } type;
    uint32_t primitive_index;  // Index into scene.spheres_ or scene.meshes_

    // 2. How bright is it?
    // We cache this so we don't have to look up the material every time
    Spectrum emission;

    // 3. Where is it? (Bounding Box for optimization)
    // BoundBox bounds;
};
}  // namespace skwr
