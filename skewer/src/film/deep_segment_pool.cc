#include "film/deep_segment_pool.h"

#include <mutex>


using namespace skwr;

DeepSegmentPool::DeepSegmentPool(size_t initial_chunks) {
    chunks_.reserve(initial_chunks);
    for (size_t i = 0; i < initial_chunks; ++i) {
        chunks_.push_back(std::make_unique<DeepSegmentNode[]>(RenderConstants::kChunkSize));
    }
}

static void DeepSegmentPool::GrowToFit(size_t chunk_index) {
    std::unique_lock<std::shared_mutex> lock(chunks_mutex_);
    // Double-check after acquiring lock
    while (chunks_.size() <= chunk_index) {
        chunks_.push_back(std::make_unique<DeepSegmentNode[]>(RenderConstants::kChunkSize));
    }
}

static size_t DeepSegmentPool::Allocate() {
    size_t index = cursor_.fetch_add(1, std::memory_order_relaxed);
    size_t chunk_index = index / RenderConstants::kChunkSize;

    {
        std::shared_lock<std::shared_mutex> lock(chunks_mutex_);
        if (chunk_index < chunks_.size()) {
            return index;
        }
    }

    GrowToFit(chunk_index);
    return index;
}

auto DeepSegmentPool::GetChunk(size_t chunk_index) -> DeepSegmentNode* {
    std::shared_lock<std::shared_mutex> lock(chunks_mutex_);
    return chunks_[chunk_index].get();
}

auto DeepSegmentPool::GetChunk(size_t chunk_index) const -> const DeepSegmentNode* {
    std::shared_lock<std::shared_mutex> lock(chunks_mutex_);
    return chunks_[chunk_index].get();
}

auto DeepSegmentPool::operator[](size_t index) -> DeepSegmentNode& {
    return GetChunk(index / RenderConstants::kChunkSize)[index % RenderConstants::kChunkSize];
}

auto DeepSegmentPool::operator[](size_t index) const -> const DeepSegmentNode& {
    return GetChunk(index / RenderConstants::kChunkSize)[index % RenderConstants::kChunkSize];
}
