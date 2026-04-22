#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "scene/interp_curve.h"

namespace skwr {

TEST(InterpCurve, LinearEndpoints) {
    const auto& L = BezierCurve::Linear();
    EXPECT_NEAR(L.Evaluate(0.0f), 0.0f, 1e-5f);
    EXPECT_NEAR(L.Evaluate(1.0f), 1.0f, 1e-5f);
}

TEST(InterpCurve, PresetsEndpoints) {
    const std::vector<const BezierCurve*> curves = {&BezierCurve::Linear(), &BezierCurve::EaseIn(),
                                                    &BezierCurve::EaseOut(),
                                                    &BezierCurve::EaseInOut()};
    for (const BezierCurve* c : curves) {
        EXPECT_NEAR(c->Evaluate(0.0f), 0.0f, 1e-5f);
        EXPECT_NEAR(c->Evaluate(1.0f), 1.0f, 1e-5f);
    }
}

TEST(InterpCurve, LinearMidpoint) {
    EXPECT_NEAR(BezierCurve::Linear().Evaluate(0.5f), 0.5f, 1e-5f);
}

TEST(InterpCurve, EaseInSlowerThanLinearAtStart) {
    float lin = BezierCurve::Linear().Evaluate(0.1f);
    float ein = BezierCurve::EaseIn().Evaluate(0.1f);
    EXPECT_LT(ein, lin);
}

TEST(InterpCurve, EaseOutFasterThanLinearAtStart) {
    float lin = BezierCurve::Linear().Evaluate(0.1f);
    float eout = BezierCurve::EaseOut().Evaluate(0.1f);
    EXPECT_GT(eout, lin);
}

TEST(InterpCurve, EaseInOutBracketed) {
    float u = 0.25f;
    float lin = BezierCurve::Linear().Evaluate(u);
    float eio = BezierCurve::EaseInOut().Evaluate(u);
    EXPECT_GT(eio, BezierCurve::EaseIn().Evaluate(u));
    EXPECT_LT(eio, BezierCurve::EaseOut().Evaluate(u));
    EXPECT_LT(std::fabs(eio - lin), 0.15f);
}

TEST(InterpCurve, CustomBezierDiagonalMatchesLinear) {
    // P1=(0,0) P2=(1,1) gives x(t)=t, y(t)=t
    BezierCurve diag(0.0f, 0.0f, 1.0f, 1.0f);
    for (float u : {0.0f, 0.1f, 0.33f, 0.9f, 1.0f}) {
        EXPECT_NEAR(diag.Evaluate(u), BezierCurve::Linear().Evaluate(u), 1e-4f);
    }
}

TEST(InterpCurve, CustomBezierAsymmetric) {
    BezierCurve c(0.2f, 0.5f, 0.8f, 0.5f);
    float y = c.Evaluate(0.5f);
    EXPECT_TRUE(std::isfinite(y));
    EXPECT_GE(y, 0.0f);
    EXPECT_LE(y, 1.0f);
}

TEST(InterpCurve, NewtonSamplesStayFiniteAndIn01) {
    const std::vector<const BezierCurve*> curves = {&BezierCurve::EaseIn(), &BezierCurve::EaseOut(),
                                                    &BezierCurve::EaseInOut()};
    for (const BezierCurve* c : curves) {
        for (float u : {0.01f, 0.05f, 0.11f, 0.37f, 0.5f, 0.63f, 0.89f, 0.99f}) {
            float y = c->Evaluate(u);
            EXPECT_TRUE(std::isfinite(y)) << "u=" << u;
            EXPECT_GE(y, -1e-4f);
            EXPECT_LE(y, 1.0f + 1e-4f);
        }
    }
}

}  // namespace skwr
