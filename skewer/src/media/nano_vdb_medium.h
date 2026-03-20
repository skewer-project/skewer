#ifndef SKWR_MEDIA_NANO_VDB_MEDIUM_H_
#define SKWR_MEDIA_NANO_VDB_MEDIUM_H_

#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/IO.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "core/math/vec3.h"
#include "core/spectral/spectral_curve.h"
#include "geometry/boundbox.h"

namespace skwr {

// RAII (Resource Acquisition Is Initialization) wrapper for zero-copy memory mapped files
class MappedFile {
    public:
    MappedFile() : data_(nullptr), size_(0) {}

    // Disable copying (Exclusive Ownership)
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // Enable moving
    MappedFile(MappedFile&& other) noexcept : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    MappedFile& operator=(MappedFile&& other) noexcept {
        if (this != &other) {
            Unmap();
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    ~MappedFile() { Unmap(); }

    // Maps a file into memory using mmap.
    bool Map(const std::string& filepath) {
        Unmap();
        int fd = open(filepath.c_str(), O_RDONLY);
        if (fd < 0) return false;

        // Gather stat information about the file to get size
        struct stat st;
        fstat(fd, &st);
        size_ = st.st_size;

        data_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);

        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            size_ = 0;
            return false;
        }

        return true;
    }

    void* GetData() const { return data_; }
    size_t GetSize() const { return size_; }

    private:
    // Unmaps the current data, if any. Called by destructor and move assignment.
    void Unmap() {
        if (data_ && data_ != MAP_FAILED) {
            munmap(data_, size_);
            data_ = nullptr;
            size_ = 0;
        }
    }

    void* data_;
    size_t size_;

};


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

    MappedFile mapped_file;

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

        // Map file directly into virtual memory
        if (!mapped_file.Map(filepath)) {
            std::cerr << "Failed to mmap NanoVDB: " << filepath << "\n";
            return false;
        }

        try {
            // Wrap the mapped pointer in a non-owning NanoVDB HostBuffer
            auto buffer = nanovdb::HostBuffer::createFull(mapped_file.GetSize(), mapped_file.GetData());

            // Create the handle and extract pointers
            handle = nanovdb::GridHandle<>(std::move(buffer));
            grid = handle.grid<float>();

            if (!grid) {
                std::cerr << "NanoVDB Grid is not a 32-bit float grid.\n";
                return false;
            }

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
            std::cerr << "Failed to parse mapped NanoVDB: " << e.what() << "\n";
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
