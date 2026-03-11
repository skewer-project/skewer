#ifndef SKWR_MEDIA_NANO_VDB_MEDIUM_H_
#define SKWR_MEDIA_NANO_VDB_MEDIUM_H_

#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/IO.h>

#include "core/spectral/spectral_curve.h"
#include "geometry/boundbox.h"

namespace skwr {

/**
 * Safe per-thread accessor lookup for cache coherence
 * NOTE: NanoVDBAccessor must not outlive the NanoVDBMedium it was built from
 */
struct NanoVDBAccessor {
    nanovdb::FloatTree::AccessorType accessor;

    explicit NanoVDBAccessor(const nanovdb::FloatTree& tree) : accessor(tree.getAccessor()) {}
};

struct NanoVDBMedium {
    SpectralCurve sigma_a_base;
    SpectralCurve sigma_s_base;

    float g;
    float max_density = 1.0f;
    float density_multiplier = 1.0f;

    // The contiguous memory buffer holding the VDB data
    nanovdb::GridHandle<> handle;
    const nanovdb::FloatGrid* grid = nullptr;
    const nanovdb::FloatTree* tree = nullptr;

    // Fast marching bounds
    BoundBox bbox;

    bool Load(const std::string& filepath) {
        try {
            handle = nanovdb::io::readGrid(filepath);
            grid = handle.grid<float>();
            if (!grid) return false;

            tree = &grid->tree();

            // Extract the exact world-space bounding box
            auto vdb_bbox = grid->worldBBox();
            bbox = BoundBox(Vec3(vdb_bbox.min()[0], vdb_bbox.min()[1], vdb_bbox.min()[2]),
                            Vec3(vdb_bbox.max()[0], vdb_bbox.max()[1], vdb_bbox.max()[2]));

            float min_val, max_val;
            tree->extrema(min_val, max_val);
            max_density = max_val * density_multiplier;  // perfect majorant

            return true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load NanoVDB: " << e.what() << "\n";
            return false;
        }
    }

    // Voxel lookup
    float GetDensity(const Point3& p_world, NanoVDBAccessor& acc) const {
        // Transform world-space ray coords into the VDB's internal 3D index space
        nanovdb::Vec3f p_index =
            grid->worldToIndexF(nanovdb::Vec3f(p_world.x(), p_world.y(), p_world.z()));

        // Nearest-Neighbor Voxel Lookup
        // TODO: Trilinear interpolation for smoother clouds maybe
        float density = acc.accessor.getValue(nanovdb::Coord::Floor(p_index));
        return density * density_multiplier;
    }
};

}  // namespace skwr

#endif  // SKWR_MEDIA_NANO_VDB_MEDIUM_H_
