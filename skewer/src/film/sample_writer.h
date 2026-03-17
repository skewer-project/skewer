#ifndef SKWR_FILM_SAMPLE_WRITER_H_
#define SKWR_FILM_SAMPLE_WRITER_H_

#include "core/containers/bounded_array.h"
#include "core/cpu_config.h"
#include "core/transport/deep_segment.h"
#include "film/film.h"

namespace skwr {

class SampleWriter {
  public:
    inline void PushDeepSegment(float z_front, float z_back, const RGB& final_rgb, float alpha) {
        deep_segments_.push_back({z_front, z_back, final_rgb, alpha});
    }

    inline void AddBeauty(const Spectrum& L, float alpha) const {
        film_->AddBeautySample(pixelIndex, L, alpha);
    }

    inline void Commit(const RGB& beauty_L, float beauty_alpha) {
        // 1. Commit Beauty
        film_->AddSample(x_, y_, beauty_L, beauty_alpha, sample_weight);

        // 2. Commit Deep (if any)
        if (!deep_segments_.empty()) {
            // We pass the local array to the film to handle the pool allocation and atomics
            film_->AddDeepSample(x_, y_, deep_segments_);
        }
    }

  private:
    Film* film_;
    int x_;
    int y_;
    float sample_weight_;
    BoundedArray<DeepSegment, kMaxDeepSegments> deep_segments_;
    bool is_adaptive_;
    bool enable_deep_;
};

}  // namespace skwr

#endif  // SKWR_FILM_SAMPLE_WRITER_H_
