#ifndef SKWR_ACCELERATORS_TLAS_H_
#define SKWR_ACCELERATORS_TLAS_H_

#include <cstdint>
#include <vector>

#include "accelerators/blas.h"
#include "accelerators/bvh.h"
#include "accelerators/instance.h"
#include "core/ray.h"
#include "core/transport/surface_interaction.h"

namespace skwr {

class TLAS {
  public:
    void Build(std::vector<Instance>& instances);

    bool Intersect(const Ray& ray, float t_min, float t_max, SurfaceInteraction* si,
                   const std::vector<BLAS>& blases, const std::vector<Instance>& instances) const;

    bool IsEmpty() const { return nodes_.empty(); }

  private:
    std::vector<BVHNode> nodes_;

    void Subdivide(uint32_t node_idx, uint32_t first_inst, uint32_t inst_count,
                   std::vector<BVHPrimitiveInfo>& primitive_info, std::vector<Instance>& instances);
};

}  // namespace skwr

#endif
