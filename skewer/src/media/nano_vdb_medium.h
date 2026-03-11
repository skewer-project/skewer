#ifndef SKWR_MEDIA_NANO_VDB_MEDIUM_H_
#define SKWR_MEDIA_NANO_VDB_MEDIUM_H_

#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/IO.h>

#include "core/spectral/spectrum.h"
#include "geometry/boundbox.h"

namespace skwr {

struct NanoVDBMedium {
    Spectrum sigma_a_base;
    Spectrum sigma_s_base;

    float g;
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
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load NanoVDB: " << e.what() << "\n";
            return false;
        }
    }
};

}  // namespace skwr

#endif  // SKWR_MEDIA_NANO_VDB_MEDIUM_H_
