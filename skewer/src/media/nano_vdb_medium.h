#ifndef SKWR_MEDIA_NANO_VDB_MEDIUM_H_
#define SKWR_MEDIA_NANO_VDB_MEDIUM_H_

#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/IO.h>

#include "core/math/vec3.h"
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

    // Spatial transforms
    float scale = 1.0f;
    Vec3 translate = {0.0f, 0.0f, 0.0f};
    Vec3 vdb_centroid = {0.0f, 0.0f, 0.0f};

    bool Load(const std::string& filepath) {
        if (scale == 0.0f || density_multiplier < 0.0f) return false;
        try {
            handle = nanovdb::io::readGrid(filepath);
            grid = handle.grid<float>();
            if (!grid) return false;

            tree = &grid->tree();

            // Extract the exact world-space bounding box
            auto vdb_bbox = grid->worldBBox();
            BoundBox original_bbox(Vec3(vdb_bbox.min()[0], vdb_bbox.min()[1], vdb_bbox.min()[2]),
                                   Vec3(vdb_bbox.max()[0], vdb_bbox.max()[1], vdb_bbox.max()[2]));

            vdb_centroid = original_bbox.Centroid();

            // 2. Map the VDB bounds to your Engine's World Space
            Vec3 min_eng = ((original_bbox.min() - vdb_centroid) * scale) + translate;
            Vec3 max_eng = ((original_bbox.max() - vdb_centroid) * scale) + translate;

            // Ensure min/max are correctly ordered
            bbox = BoundBox(
                Vec3(std::min(min_eng.x(), max_eng.x()), std::min(min_eng.y(), max_eng.y()),
                     std::min(min_eng.z(), max_eng.z())),
                Vec3(std::max(min_eng.x(), max_eng.x()), std::max(min_eng.y(), max_eng.y()),
                     std::max(min_eng.z(), max_eng.z())));

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
        if (!grid) return 0.0f;
        // --- MAP: World Space -> VDB Local Space ---
        // 1. Undo translation
        // 2. Undo scale
        // 3. Re-apply the original VDB centroid offset
        Vec3 p_vdb = ((p_world - translate) * (1.0f / scale)) + vdb_centroid;

        // Transform world-space ray coords into the VDB's internal 3D index space
        nanovdb::Vec3f p_index =
            grid->worldToIndexF(nanovdb::Vec3f(p_vdb.x(), p_vdb.y(), p_vdb.z()));

        // Nearest-Neighbor Voxel Lookup
        // TODO: Trilinear interpolation for smoother clouds maybe
        float density = acc.accessor.getValue(nanovdb::Coord::Floor(p_index));
        return density * density_multiplier;
    }

    // Helpers
    Vec3 Center() const { return bbox.Centroid(); }

    float BoundingRadius() const { return 0.5f * bbox.Diagonal().Length(); }
};

}  // namespace skwr

#endif  // SKWR_MEDIA_NANO_VDB_MEDIUM_H_
