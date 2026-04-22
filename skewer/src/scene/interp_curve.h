#ifndef SKWR_SCENE_INTERP_CURVE_H_
#define SKWR_SCENE_INTERP_CURVE_H_

namespace skwr {

// Normalized segment parameter u in [0,1] -> eased value in [0,1]
class InterpolationCurve {
  public:
    virtual ~InterpolationCurve() = default;
    virtual float Evaluate(float u) const = 0;
};

// Cubic Bezier with P0=(0,0), P3=(1,1); P1=(p1x,p1y), P2=(p2x,p2y).
class BezierCurve : public InterpolationCurve {
  public:
    BezierCurve(float p1x, float p1y, float p2x, float p2y);
    float Evaluate(float u) const override;

    static const BezierCurve& Linear();
    static const BezierCurve& EaseIn();
    static const BezierCurve& EaseOut();
    static const BezierCurve& EaseInOut();

  private:
    float p1x_, p1y_, p2x_, p2y_;

    float SampleX(float t) const;
    float SampleY(float t) const;
    float SampleDX(float t) const;
    float SolveForT(float u) const;
};

}  // namespace skwr

#endif
