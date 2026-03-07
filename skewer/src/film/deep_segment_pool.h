#ifndef SKWR_DEEP_SEGMENT_POOL_H_
#define SKWR_DEEP_SEGMENT_POOL_H_

#include <atomic>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <vector>

#include "core/color.h"

namespace skwr {

struct DeepSegmentNode {
    float z_front;
    float z_back;
    RGB L;
    float alpha;
    int next;  // logical index into the chunked pool
};

// Chunked pool allocator for DeepSegmentNodes.
// Allocates fixed-size chunks on demand to avoid a single massive upfront allocation.
// Thread-safe: atomic cursor for fast-path allocation, mutex only when growing.
class DeepSegmentPool {
  public:
    static constexpr size_t kChunkSize = 1 << 20;  // ~1M nodes per chunk (~28 MB)

    DeepSegmentPool() = default;
    explicit DeepSegmentPool(size_t initial_chunks);

    // Allocate a node and return its logical index. Thread-safe.
    size_t Allocate();

    DeepSegmentNode& operator[](size_t index);
    const DeepSegmentNode& operator[](size_t index) const;

    size_t size() const { return cursor_.load(std::memory_order_relaxed); }

  private:
    void GrowToFit(size_t chunk_index);
    DeepSegmentNode* GetChunk(size_t chunk_index);
    const DeepSegmentNode* GetChunk(size_t chunk_index) const;

    std::vector<std::unique_ptr<DeepSegmentNode[]>> chunks_;
    std::atomic<size_t> cursor_{0};
    mutable std::shared_mutex chunks_mutex_;
};
}  // namespace skwr

#endif