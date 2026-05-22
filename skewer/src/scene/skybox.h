#ifndef SKWR_SCENE_SKYBOX_H_
#define SKWR_SCENE_SKYBOX_H_

#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include "core/color/color.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/spectral/spectral_utils.h"
#include "core/spectral/spectrum.h"
#include "materials/texture.h"

namespace skwr {

enum class SkyboxFace {
    PosX = 0,
    NegX = 1,
    PosY = 2,
    NegY = 3,
    PosZ = 4,
    NegZ = 5,
};

struct SkyboxSample {
    float t = 0.0f;
    SkyboxFace face = SkyboxFace::PosX;
    float u = 0.0f;
    float v = 0.0f;
    RGB color = RGB(0.0f);
};

/// A skybox is a cube that surrounds the scene and provides a background for the rendered image.
class Skybox {
  public:
    void SetBounds(const Vec3& min_point, const Vec3& max_point) {
        min_ = min_point;
        max_ = max_point;
    }

    const Vec3& Min() const { return min_; }
    const Vec3& Max() const { return max_; }

    /// Set the texture for a specific face of the skybox.
    void SetFace(SkyboxFace face, ImageTexture&& texture) {
        faces_[FaceIndex(face)] = std::move(texture);
    }

    bool HasFace(SkyboxFace face) const { return faces_[FaceIndex(face)].IsValid(); }

    bool IsValid() const {
        if (!(max_.x() > min_.x() && max_.y() > min_.y() && max_.z() > min_.z())) {
            return false;
        }
        for (const ImageTexture& face : faces_) {
            if (face.IsValid()) return true;
        }
        return false;
    }

    bool Sample(const Ray& ray, float t_min, float t_max, SkyboxSample* out) const {
        if (!IsValid()) return false;

        // near_t and far_t represent the parametric distances along the ray
        // where it enters and exits the skybox.
        float near_t = -std::numeric_limits<float>::max();
        float far_t = std::numeric_limits<float>::max();

        // Find the intersection of the ray with the skybox
        // Treats each dimension as its own
        for (int axis = 0; axis < 3; ++axis) {
            const float inv_d = ray.inv_direction()[axis];
            // t0,t1 represent time hitting axis min or max based on ray dir
            float t0 = (min_[axis] - ray.origin()[axis]) * inv_d;
            float t1 = (max_[axis] - ray.origin()[axis]) * inv_d;
            if (inv_d < 0.0f) {  // swap values if ray is going in neg direction
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            near_t = std::fmax(near_t, t0);
            far_t = std::fmin(far_t, t1);
            if (far_t < near_t) return false;  // No intersection with the skybox
        }

        // Check if the intersection points are within the valid t range for the ray.
        // Far_t is the general hit point.
        // If the ray is outside the box, then near_t will be greater than t_min and we want to use
        // that instead.
        const float hit_t = near_t >= t_min ? near_t : far_t;

        if (hit_t < t_min || hit_t > t_max) return false;

        SkyboxSample sample;
        sample.t = hit_t;

        // actual point of itersection
        const Vec3 p = ray.at(hit_t);
        sample.face = PickFace(p);

        // Compute the UV coordinates for the hit point on the skybox face.
        FaceUV(sample.face, p, &sample.u, &sample.v);

        const ImageTexture& texture = faces_[FaceIndex(sample.face)];

        // Get the color from the texture with u and v and clamped to the edge.
        sample.color = texture.IsValid()
                           ? texture.Sample(sample.u, sample.v, TextureWrapMode::Clamp)
                           : RGB(0.0f);

        if (out != nullptr) *out = sample;
        return true;
    }

  private:
    static size_t FaceIndex(SkyboxFace face) { return static_cast<size_t>(face); }

    // Helper to get closest face
    static void ConsiderFace(SkyboxFace candidate, float distance, SkyboxFace* face, float* best) {
        if (distance < *best) {
            *best = distance;
            *face = candidate;
        }
    }

    /// Pick the face of the skybox that a point is on.
    SkyboxFace PickFace(const Vec3& p) const {
        SkyboxFace face = SkyboxFace::PosX;
        float best = std::fabs(p.x() - max_.x());

        // Checks for each face by comparing the distance of the point to the corresponding plane of
        // the skybox.
        ConsiderFace(SkyboxFace::NegX, std::fabs(p.x() - min_.x()), &face, &best);
        ConsiderFace(SkyboxFace::PosY, std::fabs(p.y() - max_.y()), &face, &best);
        ConsiderFace(SkyboxFace::NegY, std::fabs(p.y() - min_.y()), &face, &best);
        ConsiderFace(SkyboxFace::PosZ, std::fabs(p.z() - max_.z()), &face, &best);
        ConsiderFace(SkyboxFace::NegZ, std::fabs(p.z() - min_.z()), &face, &best);
        return face;
    }

    /// Compute the UV coordinates for a point on a specific face of the skybox.
    ///
    /// Convention: from the cube center, looking outward along the face normal,
    /// using a right-handed camera frame with +Y as "up" (or the closest analog
    /// for the top/bottom faces). This matches standard cubemap texture authoring:
    ///
    ///   Face   Normal   Right   Up       u = ...            v = ...
    ///   PosX   +X       -Z      +Y       (max_z - p.z)/dz   (p.y - min_y)/dy
    ///   NegX   -X       +Z      +Y       (p.z - min_z)/dz   (p.y - min_y)/dy
    ///   PosY   +Y       +X      -Z       (p.x - min_x)/dx   (max_z - p.z)/dz
    ///   NegY   -Y       +X      +Z       (p.x - min_x)/dx   (p.z - min_z)/dz
    ///   PosZ   +Z       -X      +Y       (max_x - p.x)/dx   (p.y - min_y)/dy
    ///   NegZ   -Z       +X      +Y       (p.x - min_x)/dx   (p.y - min_y)/dy
    void FaceUV(SkyboxFace face, const Vec3& p, float* u, float* v) const {
        const float dx = max_.x() - min_.x();
        const float dy = max_.y() - min_.y();
        const float dz = max_.z() - min_.z();

        switch (face) {
            case SkyboxFace::PosX:
                *u = (max_.z() - p.z()) / dz;
                *v = (p.y() - min_.y()) / dy;
                break;
            case SkyboxFace::NegX:
                *u = (p.z() - min_.z()) / dz;
                *v = (p.y() - min_.y()) / dy;
                break;
            case SkyboxFace::PosY:
                *u = (p.x() - min_.x()) / dx;
                *v = (max_.z() - p.z()) / dz;
                break;
            case SkyboxFace::NegY:
                *u = (p.x() - min_.x()) / dx;
                *v = (p.z() - min_.z()) / dz;
                break;
            case SkyboxFace::PosZ:
                *u = (max_.x() - p.x()) / dx;
                *v = (p.y() - min_.y()) / dy;
                break;
            case SkyboxFace::NegZ:
                *u = (p.x() - min_.x()) / dx;
                *v = (p.y() - min_.y()) / dy;
                break;
        }
    }

    Vec3 min_ = Vec3(-1.0f, -1.0f, -1.0f);
    Vec3 max_ = Vec3(1.0f, 1.0f, 1.0f);
    std::array<ImageTexture, 6> faces_;
};

inline Spectrum EvaluateEnvironment(const Vec3& ray_dir, const SampledWavelengths& wl) {
    // STUB: Return a faint, constant ambient light, or pure black.
    // For now, let's return pitch black so it doesn't interfere with our NEE testing.
    (void)ray_dir;
    (void)wl;
    return Spectrum(0.0f);
}

}  // namespace skwr

#endif  // SKWR_SCENE_SKYBOX_H_
