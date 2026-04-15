#ifndef SKWR_ACCELERATORS_BLAS_H_
#define SKWR_ACCELERATORS_BLAS_H_

#include <vector>

#include "accelerators/bvh.h"
#include "geometry/boundbox.h"
#include "geometry/triangle.h"

namespace skwr {

struct BLAS {
    BVH bvh;
    std::vector<Triangle> triangles;
    BoundBox local_bounds;
};

}  // namespace skwr

#endif
