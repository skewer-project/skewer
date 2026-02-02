#ifndef SKWR_GEOMETRY_TRIANGLE_H_
#define SKWR_GEOMETRY_TRIANGLE_H_

#include <cstdint>

namespace skwr {

// Storing full vertices bloats the BVH and cache
// We just storing references to the mesh here
struct Triangle {
    uint32_t mesh_id;  // ID of the mesh this belongs to
    uint32_t v_idx;    // Starting index in the index buffer (0, 3, 6...)
};

}

#endif // SKWR_GEOMETRY_TRIANGLE_H_
