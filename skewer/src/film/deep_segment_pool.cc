#include "film/deep_segment_pool.h"

using namespace skwr;

DeepSegmentPool::DeepSegmentPool(size_t initial_chunks) {
    chunks_.reserve(initial_chunks);
    for (size_t i = 0; i < initial_chunks; ++i) {
        chunks_.push_back(std::make_unique<DeepSegmentNode[]>(kChunkSize));
    }
}

void DeepSegmentPool::GrowToFit(size_t chunk_index) {
    std::lock_guard<std::mutex> lock(grow_mutex_);
    // Double-check after acquiring lock
    while (chunks_.size() <= chunk_index) {
        chunks_.push_back(std::make_unique<DeepSegmentNode[]>(kChunkSize));
    }
}

size_t DeepSegmentPool::Allocate() {
    size_t index = cursor_.fetch_add(1, std::memory_order_relaxed);
    size_t chunk_index = index / kChunkSize;
    if (chunk_index >= chunks_.size()) {
        GrowToFit(chunk_index);
    }
    return index;
}

DeepSegmentNode& DeepSegmentPool::operator[](size_t index) {
    return chunks_[index / kChunkSize][index % kChunkSize];
}

const DeepSegmentNode& DeepSegmentPool::operator[](size_t index) const {
    return chunks_[index / kChunkSize][index % kChunkSize];
}