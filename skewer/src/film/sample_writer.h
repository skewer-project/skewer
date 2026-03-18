#ifndef SKWR_FILM_SAMPLE_WRITER_H_
#define SKWR_FILM_SAMPLE_WRITER_H_

#include "core/containers/bounded_array.h"
#include "core/cpu_config.h"
#include "core/transport/deep_segment.h"
#include "film/film.h"

namespace skwr {

class SampleWriter {
  public:
    SampleWriter(Film* film, int x, int y, float weight, bool is_adaptive, bool enable_deep)
        : film_(film),
          x_(x),
          y_(y),
          sample_weight_(weight),
          is_adaptive_(is_adaptive),
          enable_deep_(enable_deep) {}

    inline void WriteBeauty(const RGB& L, float alpha) const {
        if (is_adaptive_) {
            film_->AddAdaptiveSample(x_, y_, L, alpha, sample_weight_);
        } else {
            film_->AddSample(x_, y_, L, alpha, sample_weight_);
        }
    }

    // TODO: add AOV system, or individual WriteAlbedo, WriteNormals, etc
    // inline void WriteAOV(int aov_type, const RGB& value) const {
    //         // Assuming your film has an AOV system. If not, this is how you'd hook it up.
    //         film->AddAOVSample(x, y, aov_type, value, sample_weight);
    //     }

    inline void PushDeepSegment(float z_front, float z_back, const RGB& final_rgb, float alpha) {
        deep_segments_.push_back({z_front, z_back, final_rgb, alpha});
    }

    inline void FlushDeepSegments() {
        if (enable_deep_ && !deep_segments_.empty()) {
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
