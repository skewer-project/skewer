#include "scene/interp_curve.h"

#include <algorithm>
#include <cmath>

#include "core/math/constants.h"

namespace skwr {

namespace {}  // namespace

BezierCurve::BezierCurve(float p1x, float p1y, float p2x, float p2y)
    : p1x_(p1x), p1y_(p1y), p2x_(p2x), p2y_(p2y) {}

float BezierCurve::SampleX(float t) const {
    float u = 1.0f - t;
    // x0=0, x3=1
    return 3.0f * u * u * t * p1x_ + 3.0f * u * t * t * p2x_ + t * t * t;
}

float BezierCurve::SampleY(float t) const {
    float u = 1.0f - t;
    return 3.0f * u * u * t * p1y_ + 3.0f * u * t * t * p2y_ + t * t * t;
}

float BezierCurve::SampleDX(float t) const {
    float u = 1.0f - t;
    // d/dt of x(t): P0.x=0, P3.x=1
    float term1 = p1x_ * 3.0f * (u * u - 2.0f * u * t);
    float term2 = p2x_ * 3.0f * (-t * t + 2.0f * u * t);
    return term1 + term2 + 3.0f * t * t;
}

float BezierCurve::SolveForT(float u) const {
    if (u <= 0.0f) return 0.0f;
    if (u >= 1.0f) return 1.0f;

    // Initial guess: u ~ t for monotonic timing curves
    float t = u;
    for (int i = 0; i < Bezier::kBezierNewtonMaxIter; ++i) {
        float x = SampleX(t);
        float dx = SampleDX(t);
        float f = x - u;
        if (std::fabs(f) < Bezier::kBezierNewtonEps) break;
        if (std::fabs(dx) < 1e-8f) break;
        t -= f / dx;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    return std::clamp(t, 0.0f, 1.0f);
}

float BezierCurve::Evaluate(float u) const {
    u = std::clamp(u, 0.0f, 1.0f);
    float t = SolveForT(u);
    return SampleY(t);
}

const BezierCurve& BezierCurve::Linear() {
    static const BezierCurve k(0.0f, 0.0f, 1.0f, 1.0f);
    return k;
}

const BezierCurve& BezierCurve::EaseIn() {
    static const BezierCurve k(0.42f, 0.0f, 1.0f, 1.0f);
    return k;
}

const BezierCurve& BezierCurve::EaseOut() {
    static const BezierCurve k(0.0f, 0.0f, 0.58f, 1.0f);
    return k;
}

const BezierCurve& BezierCurve::EaseInOut() {
    static const BezierCurve k(0.42f, 0.0f, 0.58f, 1.0f);
    return k;
}

}  // namespace skwr
