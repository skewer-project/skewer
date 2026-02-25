#ifndef SKWR_CORE_SAMPLING_VOLUME_STACK_H_
#define SKWR_CORE_SAMPLING_VOLUME_STACK_H_

#include <cstdint>

#include "core/cpu_config.h"

/**
 * A Priority Set
 * - LIFO stack structure to keep track of Index Of Refraction values for media the ray enters
 *
 * The "active" medium used for sampling and IOR calculation is always the medium in the
 * priorities_ array with the highest priority int, regardless of insertion order
 *
 * Keeping the array sorted by priority at insertion time makes GetActiveMedium() O(1) time
 * The active medium is queried thousands of times per ray during traversal/marching, but we only
 * Push/Pop when crossing explicit boundaries. Optimizing for the read.
 */
class VolumeStack {
  public:
    VolumeStack() : count_(0) {}

    inline void Push(uint16_t medium_id, uint16_t priority) {
        if (Contains(medium_id)) return;
        if (count_ >= kMaxMediumStack) return;  // Silent drop or log warning in debug
        // assert(count_ < kMaxMediumStack);
        // In release: maybe replace lowest-priority element

        uint8_t insert_idx = 0;
        while (insert_idx < count_ && priorities_[insert_idx] >= priority) {
            insert_idx++;
        }

        for (uint8_t i = count_; i > insert_idx; --i) {
            ids_[i] = ids_[i - 1];
            priorities_[i] = priorities_[i - 1];
        }

        ids_[insert_idx] = medium_id;
        priorities_[insert_idx] = priority;
        count_++;
    }

    inline void Pop(uint16_t medium_id) {
        for (uint8_t i = 0; i < count_; ++i) {
            if (ids_[i] == medium_id) {
                // Shift everything above it down
                for (uint8_t j = i; j < count_ - 1; ++j) {
                    ids_[j] = ids_[j + 1];
                    priorities_[j] = priorities_[j + 1];
                }
                count_--;
                return;
            }
        }
    }

    inline uint16_t GetActiveMedium() const { return (count_ > 0) ? ids_[0] : kVacuumMediumId; }

    inline bool Contains(uint16_t medium_id) const {
        for (uint8_t i = 0; i < count_; ++i)
            if (ids_[i] == medium_id) return true;
        return false;
    }

  private:
    uint16_t ids_[kMaxMediumStack];
    uint16_t priorities_[kMaxMediumStack];
    uint8_t count_;
};

#endif  // SKWR_CORE_SAMPLING_VOLUME_STACK_H_
