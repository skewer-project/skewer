#ifndef SKWR_ACCELERATORS_BVH_H_
#define SKWR_ACCELERATORS_BVH_H_

#include <cstdint>
#include <vector>

#include "geometry/boundbox.h"
#include "geometry/mesh.h"
#include "geometry/triangle.h"

namespace skwr {

/* Linear bvh - we try to preserve cache locality by ordering nodes depth-first in array */

struct alignas(32) BVHNode {
    BoundBox bounds;

    /**
     * If tri_count > 0, it's a LEAF
     *      left_first = index of first triangle in the global triangle list
     * If tri_count == 0, it's an INTERNAL NODE
     *      left_first = index of the left child node in nodes list
     *      // right child is always right next to left, so left_first + 1
     */
    uint32_t left_first;
    uint32_t tri_count;
};

class BVH {
  public:
    // Build the tree and REORDER the triangles vector for cache locality
    void Build(std::vector<Triangle>& triangles, const std::vector<Mesh>& meshes);

    const std::vector<BVHNode>& GetNodes() const { return nodes_; }

    bool IsEmpty() const { return nodes_.empty(); }

  private:
    std::vector<BVHNode> nodes_;

    // Recursive helper
    void Subdivide(uint32_t node_idx, uint32_t first_tri, uint32_t tri_count,
                   std::vector<Triangle>& triangles, const std::vector<Mesh>& meshes);
};

}  // namespace skwr

#endif  // SKWR_ACCELERATORS_BVH_H_
