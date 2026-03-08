#include "film/deep_segment_pool.h"

#include <mutex>

using namespace skwr;

DeepSegmentPool::DeepSegmentPool(size_t initial_chunks) {
    chunks_.reserve(initial_chunks);
    for (size_t i = 0; i < initial_chunks; ++i) {
        chunks_.push_back(std::make_unique<DeepSegmentNode[]>(kChunkSize));
    }
}

void DeepSegmentPool::GrowToFit(size_t chunk_index) {
    std::unique_lock<std::shared_mutex> lock(chunks_mutex_);
    // Double-check after acquiring lock
    while (chunks_.size() <= chunk_index) {
        chunks_.push_back(std::make_unique<DeepSegmentNode[]>(kChunkSize));
    }
}

size_t DeepSegmentPool::Allocate() {
    size_t index = cursor_.fetch_add(1, std::memory_order_relaxed);
    size_t chunk_index = index / kChunkSize;

    {
        std::shared_lock<std::shared_mutex> lock(chunks_mutex_);
        if (chunk_index < chunks_.size()) {
            return index;
        }
    }

    GrowToFit(chunk_index);
    return index;
}

DeepSegmentNode* DeepSegmentPool::GetChunk(size_t chunk_index) {
    std::shared_lock<std::shared_mutex> lock(chunks_mutex_);
    return chunks_[chunk_index].get();
}

const DeepSegmentNode* DeepSegmentPool::GetChunk(size_t chunk_index) const {
    std::shared_lock<std::shared_mutex> lock(chunks_mutex_);
    return chunks_[chunk_index].get();
}

DeepSegmentNode& DeepSegmentPool::operator[](size_t index) {
    return GetChunk(index / kChunkSize)[index % kChunkSize];
}

const DeepSegmentNode& DeepSegmentPool::operator[](size_t index) const {
    return GetChunk(index / kChunkSize)[index % kChunkSize];
}