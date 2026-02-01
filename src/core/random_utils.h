#ifndef SKWR_CORE_RANDOM_UTILS_H_
#define SKWR_CORE_RANDOM_UTILS_H_

#include <random>
#include <thread>

#include "core/constants.h"

namespace skwr {

// Thread-local generator to prevent locking/race conditions
inline std::mt19937& get_thread_local_generator() {
    static thread_local std::mt19937 generator = []() {
        std::random_device rd;
        // Hash the thread ID to create a unique seed per thread
        auto seed = rd() ^ (std::hash<std::thread::id>{}(std::this_thread::get_id()));
        return std::mt19937(seed);
    }();
    return generator;
}

inline Float random_float() {
    // Returns a random real in [0,1).
    static thread_local std::uniform_real_distribution<Float> distribution(0.0f, 1.0f);
    return distribution(get_thread_local_generator());
}

inline Float random_float(Float min, Float max) {
    // Returns a random real in [min,max).
    std::uniform_real_distribution<Float> distribution(min, max);
    return distribution(get_thread_local_generator());
}

}  // namespace skwr

#endif  // SKWR_CORE_RANDOM_UTILS_H_
