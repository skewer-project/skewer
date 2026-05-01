#ifndef SKWR_FILM_DEEP_BUCKET_H_
#define SKWR_FILM_DEEP_BUCKET_H_

#include <cstddef>
#include <cstdint>

namespace skwr {

// Maximum number of merged depth buckets stored per pixel. Sized to handle
// realistic scene depth complexity (a handful of overlapping surfaces and
// volumes per pixel) while keeping per-pixel deep storage bounded at compile
// time. Forced eviction kicks in when this is exceeded.
constexpr std::size_t kMaxDeepBuckets = 16;

// Distinguishes hard surfaces (per-sample alpha == 1.0) from volumes
// (fractional per-sample alpha). Set when a bucket is created and never
// changed: forced merges may contaminate but do not reclassify.
enum class DeepAlphaClass : std::uint8_t {
    Volume = 0,
    Surface = 1,
};

// A merged group of deep segments at approximately the same depth, accumulated
// online during render. r/g/b/alpha are sums over every sample path that
// landed in this bucket. Final normalization (per-pixel sample-count divide
// and front-to-back true_opacity computation) happens at write time.
struct DeepBucket {
    float z_front = 0.0f;
    float z_back = 0.0f;  // running average of incoming z_back values
    float sum_r = 0.0f;
    float sum_g = 0.0f;
    float sum_b = 0.0f;
    float sum_alpha = 0.0f;  // path count for surfaces; opacity sum for volumes
    DeepAlphaClass alpha_class = DeepAlphaClass::Surface;
};

}  // namespace skwr

#endif  // SKWR_FILM_DEEP_BUCKET_H_
