#ifndef SKWR_MEDIA_NANO_VDB_MEDIUM_H_
#define SKWR_MEDIA_NANO_VDB_MEDIUM_H_

#include <fcntl.h>
#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/IO.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <optional>

#include "core/math/vec3.h"
#include "core/spectral/spectral_curve.h"
#include "geometry/boundbox.h"

namespace skwr {

// RAII wrapper for zero-copy memory mapped files.
// This allows multiple K8s pods on the same node to share physical memory pages for large VDBs.
class MappedFile {
  public:
    MappedFile() : data_(nullptr), size_(0) {}

    // Disable copying to enforce exclusive ownership (Google Style compliant)
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

    bool Map(const std::string& filepath) {
        Unmap();
        int fd = open(filepath.c_str(), O_RDONLY);
        if (fd < 0) return false;

        struct stat st;
        fstat(fd, &st);
        size_ = st.st_size;

        // Map as private, read-only.
        // This is safe because NanoVDB is a pointer-less, flat data structure.
        data_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);

        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            size_ = 0;
            return false;
        }
        return true;
    }

    void* data() const { return data_; }
    size_t size() const { return size_; }

  private:
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

struct NanoVDBMedium;  // Forward declaration

// A unified accessor that can handle both Float and Fp16 grids.
// We use std::optional to defer construction of the Accessor (which is not default-constructible).
struct NanoVDBAccessor {
    bool is_fp16;

    // NanoVDB ReadAccessors are small and cache hierarchical nodes for speed.
    std::optional<nanovdb::FloatGrid::AccessorType> float_acc;
    std::optional<nanovdb::NanoGrid<nanovdb::Fp16>::AccessorType> fp16_acc;

    explicit NanoVDBAccessor(const NanoVDBMedium& medium);

    float GetValue(const nanovdb::Vec3f& p) const {
        nanovdb::Coord ijk = nanovdb::Coord::Floor(p);
        if (is_fp16) {
            // For Fp16, NanoVDB usually returns a float dequantized value automatically
            // depending on the grid configuration.
            return static_cast<float>(fp16_acc.value().getValue(ijk));
        }
        return float_acc.value().getValue(ijk);
    }
};

struct NanoVDBMedium {
    SpectralCurve sigma_a_base;
    SpectralCurve sigma_s_base;

    float g;
    float max_density = 1.0f;
    float density_multiplier = 1.0f;

    MappedFile mapped_file;
    nanovdb::GridHandle<> handle;

    bool is_fp16 = false;
    const nanovdb::FloatGrid* float_grid = nullptr;
    const nanovdb::NanoGrid<nanovdb::Fp16>* fp16_grid = nullptr;

    BoundBox bbox;
    float scale = 1.0f;
    Vec3 translate = {0.0f, 0.0f, 0.0f};
    Vec3 vdb_centroid = {0.0f, 0.0f, 0.0f};

    bool Load(const std::string& filepath) {
        if (scale == 0.0f || density_multiplier < 0.0f) return false;

        // Memory Map the file (Zero-copy virtual memory loading)
        if (!mapped_file.Map(filepath)) {
            std::cerr << "Failed to mmap NanoVDB: " << filepath << "\n";
            return false;
        }

        try {
            const uint8_t* file_data = static_cast<const uint8_t*>(mapped_file.data());
            const uint8_t* grid_data = nullptr;

            // .nvdb files contain a File Header and a Dictionary before the actual Grid.
            // Depending on the exporter, the grid data might not be padded to a 32-byte
            // boundary in the file stream (e.g., Header is 32B + Dict is 48B = 80B offset).
            // Scanning every 8 bytes ensures we don't vault over the magic number.
            for (size_t offset = 0; offset + sizeof(nanovdb::GridMetaData) <= mapped_file.size();
                 offset += 8) {
                const uint64_t magic = *reinterpret_cast<const uint64_t*>(file_data + offset);

                // Check for NanoVDB Grid Magic (Handles both pre-32.6 and post-32.6 version
                // formats)
                if (magic == NANOVDB_MAGIC_GRID || magic == NANOVDB_MAGIC_NUMB) {
                    grid_data = file_data + offset;
                    break;
                }
            }

            if (!grid_data) {
                std::cerr << "Could not locate a valid NanoVDB grid within the mapped file.\n";
                return false;
            }

            // Get the exact size of the grid directly from its internal metadata
            const nanovdb::GridMetaData* meta_data =
                reinterpret_cast<const nanovdb::GridMetaData*>(grid_data);

            // Check if the memory address is perfectly 32-byte aligned
            if (reinterpret_cast<uintptr_t>(grid_data) % 32 == 0) {
                // Zero-copy wrapping
                auto buffer = nanovdb::HostBuffer::createFull(meta_data->gridSize(),
                                                              const_cast<uint8_t*>(grid_data));
                handle = nanovdb::GridHandle<>(std::move(buffer));
            } else {
                // Fallback Path: The file exporter didn't pad the dictionary.
                // We MUST allocate an aligned buffer and copy the data to avoid AVX segfaults.
                std::cerr << "[skewer] Warning: NanoVDB grid in '" << filepath
                          << "' is not 32-byte aligned. Falling back to memory copy.\n";

                auto buffer = nanovdb::HostBuffer::create(meta_data->gridSize());
                std::memcpy(buffer.data(), grid_data, meta_data->gridSize());
                handle = nanovdb::GridHandle<>(std::move(buffer));
            }

            auto* meta = handle.gridMetaData();

            nanovdb::math::BBox<nanovdb::math::Vec3d> vdb_bbox;

            // Handle both 32-bit Float and 16-bit Quantized grids
            if (meta->gridType() == nanovdb::GridType::Float) {
                is_fp16 = false;
                float_grid = handle.grid<float>();
                vdb_bbox = float_grid->worldBBox();

                float min_val, max_val;
                float_grid->tree().extrema(min_val, max_val);
                max_density = max_val * density_multiplier;

            } else if (meta->gridType() == nanovdb::GridType::Fp16) {
                is_fp16 = true;
                fp16_grid = handle.grid<nanovdb::Fp16>();
                vdb_bbox = fp16_grid->worldBBox();

                // Fp16 grids in NanoVDB often use float as their ValueType API
                float min_v, max_v;
                fp16_grid->tree().extrema(min_v, max_v);
                max_density = max_v * density_multiplier;

            } else {
                std::cerr << "Unsupported NanoVDB grid type! Must be Float or Fp16.\n";
                return false;
            }

            // Extract world-space bounding box and centroid
            BoundBox original_bbox(Vec3(vdb_bbox.min()[0], vdb_bbox.min()[1], vdb_bbox.min()[2]),
                                   Vec3(vdb_bbox.max()[0], vdb_bbox.max()[1], vdb_bbox.max()[2]));

            vdb_centroid = original_bbox.Centroid();

            // Map VDB bounds to Engine World Space (Scale then Translate)
            Vec3 min_eng = ((original_bbox.min() - vdb_centroid) * scale) + translate;
            Vec3 max_eng = ((original_bbox.max() - vdb_centroid) * scale) + translate;

            bbox = BoundBox(
                Vec3(std::min(min_eng.x(), max_eng.x()), std::min(min_eng.y(), max_eng.y()),
                     std::min(min_eng.z(), max_eng.z())),
                Vec3(std::max(min_eng.x(), max_eng.x()), std::max(min_eng.y(), max_eng.y()),
                     std::max(min_eng.z(), max_eng.z())));

            return true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to parse mapped NanoVDB: " << e.what() << "\n";
            return false;
        }
    }

    float GetDensity(const Point3& p_world, const NanoVDBAccessor& acc) const {
        if (!float_grid && !fp16_grid) return 0.0f;

        Vec3 p_vdb = ((p_world - translate) * (1.0f / scale)) + vdb_centroid;
        nanovdb::Vec3f p_index;

        if (is_fp16) {
            p_index = fp16_grid->worldToIndexF(nanovdb::Vec3f(p_vdb.x(), p_vdb.y(), p_vdb.z()));
        } else {
            p_index = float_grid->worldToIndexF(nanovdb::Vec3f(p_vdb.x(), p_vdb.y(), p_vdb.z()));
        }

        return acc.GetValue(p_index) * density_multiplier;
    }

    Vec3 Center() const { return bbox.Centroid(); }
    float BoundingRadius() const {
        Vec3 d = bbox.Diagonal();
        return 0.5f * std::max({d.x(), d.y(), d.z()});
    }
};

// Accessor constructor implementation
inline NanoVDBAccessor::NanoVDBAccessor(const NanoVDBMedium& medium) : is_fp16(medium.is_fp16) {
    if (is_fp16) {
        fp16_acc.emplace(medium.fp16_grid->getAccessor());
    } else {
        float_acc.emplace(medium.float_grid->getAccessor());
    }
}

}  // namespace skwr

#endif  // SKWR_MEDIA_NANO_VDB_MEDIUM_H_
