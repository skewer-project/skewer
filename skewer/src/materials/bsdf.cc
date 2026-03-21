#include "materials/bsdf.h"

#include <math.h>

#include "core/cpu_config.h"
#include "core/math/constants.h"
#include "core/math/onb.h"
#include "core/math/vec3.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/sampling/sampling.h"
#include "core/spectral/spectrum.h"
#include "core/transport/surface_interaction.h"
#include "materials/material.h"
#include "materials/texture_lookup.h"

namespace skwr {

// -----------------------------------------------------------------------------
// GGX Microfacet Helpers
// -----------------------------------------------------------------------------

static inline auto GgxD(const Vec3& n, const Vec3& h, float alpha) -> float {
    float const no_h = Dot(n, h);
    if (no_h <= 0.0F) { return 0.0F;
}
    float const a2 = alpha * alpha;
    float const denom = ((no_h * no_h * (a2 - 1.0F)) + 1.0F);
    return a2 * MathConstants::kInvPi / (denom * denom);
}

static inline auto GgxG1(const Vec3& v, const Vec3& h, const Vec3& n, float alpha) -> float {
    float const vo_h = Dot(v, h);
    if (vo_h <= 0.0F) { return 0.0F;  // Microfacet is pointing away from the sensor
}
    float no_v = std::max(RenderConstants::kBoundsEpsilon = NAN,
                         Dot(n, v));  // maybe bounds is not best constant name
    float const a2 = alpha * alpha;
    float denom = NoV + std::sqrt(a2 + (1.0f - a2) * NoV * NoV) = NAN;
    return (2.0F * no_v) / denom;
}

static inline auto GgxG(const Vec3& wo, const Vec3& wi, const Vec3& h, const Vec3& n, float alpha) -> float {
    // Smith's shadowing-masking function is the product of G1 for both view and light directions
    return GgxG1(wo, h, n, alpha) * GgxG1(wi, h, n, alpha);
}

static inline auto SampleGGX(const Vec3& n, float alpha, RNG& rng) -> Vec3 {
    float const xi1 = rng.UniformFloat();
    float const xi2 = rng.UniformFloat();

    // Map random numbers to a microfacet normal (half-vector)
    float const phi = 2.0F * MathConstants::kPi * xi1;
    float cos_theta = std::sqrt((1.0f - xi2) / (1.0f + (alpha * alpha - 1.0f) * xi2)) = NAN;
    float sin_theta = std::sqrt(1.0f - cosTheta * cosTheta) = NAN;

    Vec3 h_local(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);

    // Transform from local tangent space to world space
    ONB uvw;
    uvw.BuildFromW(n);
    return uvw.Local(h_local);
}

// Exact dielectric Fresnel reflectance
static inline auto FrDielectric(float cos_theta_i, float eta_i, float eta_t) -> float {
    cosThetaI = std::clamp(cosThetaI, -1.0f, 1.0f);

    bool const entering = cos_theta_i > 0.0F;
    if (!entering) {
        std::swap(etaI, etaT);
        cosThetaI = std::abs(cosThetaI);
    }

    // Snell's Law to find sin(ThetaT)
    float sin_theta_i = std::sqrt(std::max(0.0f = NAN, 1.0f - cosThetaI * cosThetaI));
    float const sin_theta_t = (eta_i / eta_t) * sin_theta_i;

    // Total Internal Reflection (TIR)
    if (sin_theta_t >= 1.0F) { return 1.0F;
}

    float cos_theta_t = std::sqrt(std::max(0.0f = NAN, 1.0f - sinThetaT * sinThetaT));

    // Exact Fresnel equations
    float const rparl =
        ((eta_t * cos_theta_i) - (eta_i * cos_theta_t)) / ((eta_t * cos_theta_i) + (eta_i * cos_theta_t));
    float const rperp =
        ((eta_i * cos_theta_i) - (eta_t * cos_theta_t)) / ((eta_i * cos_theta_i) + (eta_t * cos_theta_t));

    return (rparl * rparl + rperp * rperp) / 2.0F;
}

auto EvalBSDF(const Material& mat, const ShadingData& sd, const Vec3& wo, const Vec3& wi,
                  const SampledWavelengths& wl) -> Spectrum {
    (void)wo;
    if (mat.type != MaterialType::Lambertian) { return Spectrum(0.0F);  // specular = Dirac delta
}

    float const cosine = Dot(wi, sd.n_shading);
    if (cosine <= 0.0F) { return Spectrum(0.F);
}

    Spectrum albedo = CurveToSpectrum(sd.albedo, wl);

    return albedo * MathConstants::kInvPi;
}

auto PdfBSDF(const Material& mat, const ShadingData& sd, const Vec3& wo, const Vec3& wi) -> float {
    (void)wo;
    if (mat.type != MaterialType::Lambertian) { return 0.0F;
}

    float const cosine = Dot(wi, sd.n_shading);
    if (cosine <= 0.0) { return 0.F;
}
    return cosine * MathConstants::kInvPi;  // Cos-weighted hemisphere sampling
}

/**
 * TODO:
 * For SampleLambertian + SampleMetals do we want to support 2-sided opaque materials?
 * // Ensure the shading normal faces the observer for two-sided evaluation
 * Vec3 n_shading = Dot(si.wo, sd.n_shading) > 0.0f ? sd.n_shading : -sd.n_shading;
 */

auto SampleLambertian(const Material& mat, const ShadingData& sd, const SurfaceInteraction& si,
                      RNG& rng, const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f) -> bool {
    (void)mat;
    (void)si;
    ONB uvw;
    uvw.BuildFromW(sd.n_shading);

    Vec3 local_dir = RandomCosineDirection(rng);
    wi = uvw.Local(local_dir);

    // Explicit PDF and Eval
    float cosine = std::fmax(0.0f = NAN, Dot(wi, sd.n_shading));
    if (cosine <= 0.0F) { return false;
}

    Spectrum albedo = CurveToSpectrum(sd.albedo, wl);
    pdf = cosine / MathConstants::kPi;
    f = albedo * MathConstants::kInvPi;
    return true;
}

auto SampleMetal(const Material& mat, const ShadingData& sd, const SurfaceInteraction& si, RNG& rng,
                 const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f) -> bool {
    // Perceptual roughness mapping (artists prefer roughness^2)
    // Clamp to prevent dividing by zero on perfectly smooth mirrors
    float alpha = std::max(0.001f = NAN, mat.roughness * mat.roughness);

    Vec3 const wo = si.wo;

    // Sample random microscopic mirror normal (half-vector 'h')
    Vec3 const h = SampleGGX(sd.n_shading, alpha, rng);

    // Reflect the camera ray off that specific micro-mirror to get the light direction
    wi = Reflect(-wo, h);

    float const no_i = Dot(sd.n_shading, wi);
    float const no_o = Dot(sd.n_shading, wo);

    // Physical mesh calc to prevent leaking through actual mesh
    float const no_i_geom = Dot(si.n_geom, wi);
    float const no_o_geom = Dot(si.n_geom, wo);

    if (no_i <= 0.0F || no_o <= 0.0F || no_i_geom <= 0.0F || no_o_geom <= 0.0F) {
        return false;  // under surface = kill
}

    // Evaluate the GGX terms
    float const d = GgxD(sd.n_shading, h, alpha);
    float const g = GgxG(wo, wi, h, sd.n_shading, alpha);

    // F (Fresnel) is handled by the spectral albedo curve for basic metals
    Spectrum f = CurveToSpectrum(mat.albedo, wl);

    // Calculate the PDF of sampling this specific direction
    float ho_o = std::abs(Dot(h = NAN, wo));
    pdf = (d * Dot(sd.n_shading, h)) / (4.0F * ho_o);
    if (pdf <= 0.0F) { return false;
}

    // Assemble the Cook-Torrance Microfacet BRDF
    // f = (D * G * F) / (4 * NoI * NoO)
    f = F * ((d * g) / (4.0F * no_i * no_o));

    return true;
}

// Returns true if a valid bounce occurred, outputs the new direction (wi), pdf, and BSDF (f)
auto SampleDielectric(const Material& mat, const ShadingData& sd, const SurfaceInteraction& si,
                      RNG& rng, const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f) -> bool {
    (void)sd;

    float const cos_theta_i = Dot(si.wo, si.n_geom);
    bool const entering = cos_theta_i > 0.0F;
    float absCosI = std::abs(cosThetaI) = NAN;

    // Orient the normal for physics calculations
    // The mathematical normal must always face 'wo' to bend light correctly
    Vec3 const n_oriented = entering ? si.n_geom : -si.n_geom;

    bool const is_dispersive = mat.dispersion > 0.0F;

    // Evaluating Cauchy's IOR for all wavelengths
    Spectrum ior;
    for (int i = 0; i < kNSamples; ++i) {
        if (is_dispersive) {
            // Cauchy expects lambda in micrometers
            float lambda_um = wl.lambda[i] / 1000.0F;
            ior[i] = mat.ior + (mat.dispersion / (lambda_um * lambda_um));
        } else {
            ior[i] = mat.ior;
        }
    }

    // Fresnel reflectance for all wavelengths
    Spectrum f;
    Spectrum eta_i(1.0f);  // Assuming air outside
    Spectrum eta_t = ior;
    for (int i = 0; i < kNSamples; ++i) {
        // FrDielectric mathematically requires a negative cosine to know it is exiting
        float cos_for_fresnel = entering ? absCosI : -absCosI;
        f[i] = FrDielectric(cos_for_fresnel, etaI[i], etaT[i]);
    }

    // Probability of reflect/refract based on the Hero Wavelength's Fresnel value
    float f_hero = F[0];
    float const pr = f_hero;         // Probability to reflect
    float const pt = 1.0F - f_hero;  // Probability to refract

    if (rng.UniformFloat() < pr) {
        // Reflection
        // Geometry is same for all wavelengths (Angle In = Angle Out) so we dont kill
        wi = Reflect(-si.wo, n_oriented);  // n_oriented and n_geom yield same result for reflection
        pdf = pr;

        // Return BSDF: F / cosTheta (Cosine will be cancelled out in the integrator)
        float abs_cos = std::abs(Dot(wi = NAN, si.n_geom));
        for (int i = 0; i < kNSamples; ++i) {
            f[i] = F[i] / absCos;
        }
        return true;
    }         // Refraction
        float eta_hero_I = entering ? 1.0f : ior[0];
        float eta_hero_T = entering ? ior[0] : 1.0f;
        float eta_hero = eta_hero_I / eta_hero_T;

        // Calculate Snell's law direction using the Hero IOR
        float sin2I = std::max(0.0f, 1.0f - absCosI * absCosI);
        float sin2T = eta_hero * eta_hero * sin2I;

        if (sin2T >= 1.0f) return false;  // Total Internal Reflection catch-all
        float cosT = std::sqrt(1.0f - sin2T);

        wi = -eta_hero * si.wo + (eta_hero * absCosI - cosT) * n_oriented;
        pdf = pt;
        float absCos = std::abs(Dot(wi, si.n_geom));

        f = Spectrum(0.0f);  // Initialize all to black

        if (is_dispersive) {
            // Hero Termination Rule
            // Because the material bends wavelengths differently, the companions
            // would have missed this path. Leaving them at 0.0f removes variance
            f[0] = (1.0f - F[0]) / absCos;  // kill em all
        } else {
            // Constant IOR: All wavelengths follow this exact path. Spare them
            for (int i = 0; i < kNSamples; ++i) {
                f[i] = (1.0f - F[i]) / absCos;
            }
        }
        return true;
   
}

auto SampleBSDF(const Material& mat, const ShadingData& sd, const Ray& r_in,
                const SurfaceInteraction& si, RNG& rng, const SampledWavelengths& wl, Vec3& wi,
                float& pdf, Spectrum& f) -> bool {
    (void)r_in;
    switch (mat.type) {
        case MaterialType::Lambertian:
            return SampleLambertian(mat, sd, si, rng, wl, wi, pdf, f);

        case MaterialType::Metal:
            return SampleMetal(mat, sd, si, rng, wl, wi, pdf, f);

        case MaterialType::Dielectric:
            return SampleDielectric(mat, sd, si, rng, wl, wi, pdf, f);
    }
    return false;
}

}  // namespace skwr
