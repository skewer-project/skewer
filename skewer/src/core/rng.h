#ifndef SKWR_CORE_RNG_H_
#define SKWR_CORE_RNG_H_

#include <algorithm>
#include <cstdint>

#include "core/constants.h"

namespace skwr {

// PCG32 generator is better than mt19937 ( and smaller)
class RNG {
  public:
    // Default constructor (can be used for unseeded temp generators)
    RNG() : state_(0x853c49e6748fea9bULL), inc_(0xda3e39cb94b95bdbULL) {}

    // Deterministic Constructor:
    // sequence_index: Ideally the Pixel Index (y * width + x)
    // offset: Ideally the Sample Index (0 to samples_per_pixel)
    RNG(uint64_t sequence_index, uint64_t offset) {
        state_ = 0u;
        inc_ = (sequence_index << 1u) | 1u;
        UniformUInt32();
        state_ += offset;
        UniformUInt32();
    }

    // Returns a random unsigned integer
    uint32_t UniformUInt32() {
        uint64_t oldstate = state_;
        state_ = oldstate * 6364136223846793005ULL + inc_;
        uint32_t xorshifted = uint32_t(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = uint32_t(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31));
    }

    // Returns float in [0, 1)
    float UniformFloat() {
        // High-performance float conversion
        // Multiplies by 1.0 / 2^32
        // 0x1p-32f is a hex constant for 1.0 / 2^32
        // OneMinusEpsilon prevents returning 1.0f bug (rare but possible)
        return std::min(float(UniformUInt32() * 0x1p-32f), float(kOneMinusEpsilon));
    }

  private:
    uint64_t state_;  // 8 bytes
    uint64_t inc_;    // 8 bytes
};

}  // namespace skwr

#endif  // SKWR_CORE_RNG_H_
