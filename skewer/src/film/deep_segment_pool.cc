#include "film/deep_segment_pool.h"
#include <iostream>

using namespace skwr;

DeepSegmentPool::DeepSegmentPool() : max_chunks_(64) {
    std::lock_guard<std::mutex> lock(grow_mutex_);
    chunks_.reserve(max_chunks_);
}

DeepSegmentPool::DeepSegmentPool(size_t max_chunks) 
    : max_chunks_(max_chunks) {
    // We don't allocate everything upfront, but reserve enough vector space
    // to avoid reallocation invalidating pointers to unique_ptrs.
    std::lock_guard<std::mutex> lock(grow_mutex_);
    chunks_.reserve(max_chunks_);
}

void DeepSegmentPool::GrowToFit(size_t chunk_index) {
    std::lock_guard<std::mutex> lock(grow_mutex_);
    
    // Hard limit check
    if (chunk_index >= max_chunks_) {
        return; 
    }

    // Double-check after acquiring lock
    while (chunks_.size() <= chunk_index) {
        try {
            chunks_.push_back(std::make_unique<DeepSegmentNode[]>(kChunkSize));
        } catch (const std::bad_alloc& e) {
            std::cerr << "[SKEWER] Critical: OOM when allocating deep pool chunk!\n";
            max_chunks_ = chunks_.size(); // Cap it here
            return;
        }
    }
}

size_t DeepSegmentPool::Allocate() {
    size_t index = cursor_.fetch_add(1, std::memory_order_relaxed);
    size_t chunk_index = index / kChunkSize;

    // Safety check: is this index within our hard cap?
    if (chunk_index >= max_chunks_) {
        // We've hit the limit. Try to fetch_sub but other threads might have 
        // already incremented past us. Just return failure and let Film handle it.
        return static_cast<size_t>(-1);
    }

    // Fast check: is the chunk already there?
    // We need to be careful here: reading chunks_.size() is mostly safe if we 
    // reserved the capacity, but let's be rigorous.
    if (chunk_index >= chunks_.size()) {
        GrowToFit(chunk_index);
        
        // Final check: did growth actually succeed?
        if (chunk_index >= chunks_.size()) {
            return static_cast<size_t>(-1);
        }
    }

    return index;
}

DeepSegmentNode& DeepSegmentPool::operator[](size_t index) {
    // If we've hit OOM, this will crash. We rely on Film not to use 
    // the index if Allocate returned -1.
    return chunks_[index / kChunkSize][index % kChunkSize];
}

const DeepSegmentNode& DeepSegmentPool::operator[](size_t index) const {
    return chunks_[index / kChunkSize][index % kChunkSize];
}
