#ifndef SKWR_CORE_TRANSPORT_DEEP_PATH_RECORDER_H_
#define SKWR_CORE_TRANSPORT_DEEP_PATH_RECORDER_H_

#include <vector>

#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/spectral/spectral_utils.h"
#include "core/spectral/spectrum.h"
#include "film/sample_writer.h"

namespace skwr {

inline float CameraDepth(const Ray& ray, float t, const Vec3& cam_origin, const Vec3& cam_forward) {
    Vec3 o = ray.origin() - cam_origin;
    return Dot(o, cam_forward) + t * Dot(ray.direction(), cam_forward);
}

// struct for Deferred Backwards Pass for Deep output
struct PathVertex {
    float t_start;
    float t_end;
    Spectrum local_L;                       // Local NEE + Emission ONLY (Unattenuated)
    Spectrum bsdf_weight = Spectrum(1.0f);  // The local BSDF or Phase throughput (f * cos / pdf)
    float alpha;                            // Opacity of the surface or 1.0 - Tr for volumes
    bool is_camera_path;                    // Should this vertex become a deep segment?
    bool is_volume_scatter = false;
};

class DeepPathRecorder {
  public:
    DeepPathRecorder(int max_depth) { path_vertices_.reserve(max_depth); }

    bool IsEmpty() const { return path_vertices_.empty(); }
    void UpdateBSDFWeight(Spectrum& num_beta, Spectrum& denom_beta) {
        if (!IsEmpty()) {
            // Extracts the (f * cos / pdf * RR) weight
            path_vertices_.back().bsdf_weight = num_beta / denom_beta;
        }
    }

    void AppendVertex(float t_start, float t_end, Spectrum local_L, float vertex_alpha,
                      bool is_camera_path, bool is_volume_scatter) {
        PathVertex v;
        v.t_start = t_start;
        v.t_end = t_end;
        v.local_L = local_L;
        v.alpha = vertex_alpha;
        v.is_camera_path = is_camera_path;
        v.is_volume_scatter = is_volume_scatter;
        path_vertices_.push_back(v);
    }

    void ResolveToDeep(SampleWriter& writer, const Ray& ray, const Vec3& cam_w,
                       const SampledWavelengths& wl) {
        Spectrum deep_L(0.0f);

        // Iterate backwards from the end of the path to the camera
        for (int i = (int)path_vertices_.size() - 1; i >= 0; --i) {
            const PathVertex& v = path_vertices_[i];

            // Rendering Equation: L_out = L_local + weight * L_incoming
            deep_L = v.local_L + (v.bsdf_weight * deep_L);

            if (v.is_camera_path) {
                float deep_alpha = v.alpha;
                // If the NEXT vertex left the camera path, then THIS vertex is the
                // deflection point (scattering event or reflection). It acts as an opaque
                // terminator for this specific Monte Carlo sample's line of sight
                if (i + 1 < (int)path_vertices_.size() && !path_vertices_[i + 1].is_camera_path) {
                    deep_alpha = 1.0f;
                }

                // If this is the absolute end of the traced path, but we are still on the
                // camera path, it means the path was killed (RR, Max Depth) before hitting
                // an opaque background, so we seal the alpha to prevent checkerboard bleeding
                if (i + 1 == (int)path_vertices_.size()) {
                    if (!v.is_volume_scatter) {
                        deep_alpha = 1.0f;
                    }
                }

                // Segment with its own emission, NEE, and all indirect GI for this specific depth.
                float z_front = CameraDepth(ray, v.t_start, ray.origin(), cam_w);
                float z_back = CameraDepth(ray, v.t_end, ray.origin(), cam_w);

                if (z_back >= 0.0f) {
                    if (z_front < 0.0f) z_front = 0.0f;

                    RGB final_rgb = SpectrumToRGB(deep_L, wl);

                    // Push directly to the writer's local BoundedArray
                    writer.PushDeepSegment(z_front, z_back, final_rgb, deep_alpha);
                }
                // Deep Compositing Reset
                // Because the deep compositing software uses v.alpha to blend
                // whatever is behind this segment, we MUST reset the accumulator to 0.0f here.
                // Otherwise, the background light will be embedded in the foreground segment,
                // and Nuke will composite the background over itself twice.
                deep_L = Spectrum(0.0f);
            }
        }
    }

  private:
    std::vector<PathVertex> path_vertices_;
};

}  // namespace skwr

#endif  // SKWR_CORE_TRANSPORT_DEEP_PATH_RECORDER_H_
