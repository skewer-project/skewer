#include "io/graph_from_json.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "core/math/vec3.h"
#include "scene/interp_curve.h"

using json = nlohmann::json;

namespace skwr {

namespace {

Vec3 ParseVec3(const json& j) {
    if (!j.is_array() || j.size() != 3) {
        throw std::runtime_error("Expected array of 3 numbers for Vec3");
    }
    return Vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

void ApplyScaleField(const json& scale_field, Vec3& out_s) {
    if (scale_field.is_number()) {
        float s = scale_field.get<float>();
        out_s = Vec3(s, s, s);
    } else {
        out_s = ParseVec3(scale_field);
    }
}

void PatchTRSFields(const json& j, Vec3& t, Vec3& rotate_deg, Vec3& s) {
    if (j.contains("translate")) {
        t = ParseVec3(j["translate"]);
    }
    if (j.contains("rotate")) {
        rotate_deg = ParseVec3(j["rotate"]);
    }
    if (j.contains("scale")) {
        ApplyScaleField(j["scale"], s);
    }
}

std::shared_ptr<const InterpolationCurve> SharedPreset(const BezierCurve& preset) {
    return std::shared_ptr<const InterpolationCurve>(&preset, [](const InterpolationCurve*) {});
}

}  // namespace

TRS ParseTRSFields(const json& j) {
    Vec3 t(0.0f, 0.0f, 0.0f);
    Vec3 r(0.0f, 0.0f, 0.0f);
    Vec3 s(1.0f, 1.0f, 1.0f);
    PatchTRSFields(j, t, r, s);
    return TRSFromEuler(t, r, s);
}

std::shared_ptr<const InterpolationCurve> ParseCurveJson(const json& j) {
    if (j.is_string()) {
        std::string s = j.get<std::string>();
        if (s == "linear") {
            return SharedPreset(BezierCurve::Linear());
        }
        if (s == "ease-in") {
            return SharedPreset(BezierCurve::EaseIn());
        }
        if (s == "ease-out") {
            return SharedPreset(BezierCurve::EaseOut());
        }
        if (s == "ease-in-out") {
            return SharedPreset(BezierCurve::EaseInOut());
        }
        throw std::runtime_error("Unknown interpolation curve name: " + s);
    }
    if (j.is_object() && j.contains("bezier")) {
        const auto& a = j.at("bezier");
        if (!a.is_array() || a.size() != 4) {
            throw std::runtime_error("'bezier' must be an array of 4 numbers");
        }
        return std::make_shared<BezierCurve>(a[0].get<float>(), a[1].get<float>(),
                                             a[2].get<float>(), a[3].get<float>());
    }
    throw std::runtime_error("Curve must be a preset string or {\"bezier\":[...]} object");
}

AnimatedTransform ParseAnimatedTransformJson(const json& t) {
    AnimatedTransform anim;
    if (t.contains("keyframes")) {
        const auto& kfs = t.at("keyframes");
        if (!kfs.is_array() || kfs.empty()) {
            throw std::runtime_error("'keyframes' must be a non-empty array");
        }
        Vec3 cur_t(0.0f, 0.0f, 0.0f);
        Vec3 cur_r(0.0f, 0.0f, 0.0f);
        Vec3 cur_s(1.0f, 1.0f, 1.0f);
        for (const auto& kf : kfs) {
            PatchTRSFields(kf, cur_t, cur_r, cur_s);
            Keyframe k;
            k.time = kf.at("time").get<float>();
            k.transform = TRSFromEuler(cur_t, cur_r, cur_s);
            if (kf.contains("curve")) {
                k.curve = ParseCurveJson(kf["curve"]);
            } else {
                k.curve = SharedPreset(BezierCurve::Linear());
            }
            anim.keyframes.push_back(k);
        }
        anim.SortKeyframes();
        return anim;
    }

    Keyframe k;
    k.time = 0.0f;
    k.transform = ParseTRSFields(t);
    k.curve = SharedPreset(BezierCurve::Linear());
    anim.keyframes.push_back(k);
    return anim;
}

}  // namespace skwr
