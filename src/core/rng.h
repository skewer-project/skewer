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
    RNG() : state_(kRNGStateSeed), inc_(kRNGIncSeed) {}

    // Deterministic Constructor:
    // sequence_index: Ideally the Pixel Index (y * width + x)
    // offset: Ideally the Sample Index (0 to samples_per_pixel)
    RNG(uint64_t sequence_index, uint64_t offset) : state_(0U), inc_((sequence_index << 1U) | 1U) {
        UniformUInt32();
        state_ += offset;
        UniformUInt32();
    }

    // Returns a random unsigned integer
    auto UniformUInt32() -> uint32_t {
        uint64_t oldstate = state_;
        state_ = oldstate * kPCGMultiplier + inc_;
        auto xorshifted =
            static_cast<uint32_t>(((oldstate >> kPCGShift1) ^ oldstate) >> kPCGShift2);
        auto rot = static_cast<uint32_t>(oldstate >> kPCGShift3);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1U) & kPCGRotationMask));
    }

    // Returns float in [0, 1)
    auto UniformFloat() -> float {
        // High-performance float conversion
        // Multiplies by 1.0 / 2^32
        // 0x1p-32f is a hex constant for 1.0 / 2^32
        // OneMinusEpsilon prevents returning 1.0f bug (rare but possible)
        return std::min(static_cast<float>(UniformUInt32()) * kInv2Pow32, kOneMinusEpsilon);
    }

  private:
    static constexpr uint64_t kPCGMultiplier = 6364136223846793005ULL;
    static constexpr uint32_t kPCGShift1 = 18U;
    static constexpr uint32_t kPCGShift2 = 27U;
    static constexpr uint32_t kPCGShift3 = 59U;
    static constexpr uint32_t kPCGRotationMask = 31U;
    static constexpr float kInv2Pow32 = 0x1p-32F;

    uint64_t state_;  // 8 bytes
    uint64_t inc_;    // 8 bytes
};

}  // namespace skwr

#endif  // SKWR_CORE_RNG_H_
