#ifndef SKWR_ACCELERATORS_BLAS_H_
#define SKWR_ACCELERATORS_BLAS_H_

#include <vector>

#include "accelerators/bvh.h"
#include "geometry/boundbox.h"
#include "geometry/triangle.h"

namespace skwr {

// Bottom-Level Acceleration Structure: a BVH over a single mesh's triangles in local space.
// Multiple Instances can reference the same BLAS with different transform chains.
struct BLAS {
    BVH bvh;
    std::vector<Triangle> triangles;
    BoundBox local_bounds;
};

}  // namespace skwr

#endif
