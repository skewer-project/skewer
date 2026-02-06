#ifndef SKWR_GEOMETRY_SHAPE_H_
#define SKWR_GEOMETRY_SHAPE_H_

#include <cstdint>

namespace skwr {

enum class PrimitiveType : uint8_t {
    Sphere,
    Triangle,
};

// The shape reference (abstraction for BVH)
struct PrimitiveRef {
    uint32_t index;      // Index into type-specific array
    PrimitiveType type;  // what array to index
};

}  // namespace skwr

#endif  // SKWR_GEOMETRY_SHAPE_H_
