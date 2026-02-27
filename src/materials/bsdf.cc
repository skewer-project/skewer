#include "materials/bsdf.h"

#include "core/constants.h"
#include "core/onb.h"
#include "core/sampling.h"
#include "core/spectral/spectral_utils.h"

namespace skwr {

// -----------------------------------------------------------------------------
// GGX Microfacet Helpers
// -----------------------------------------------------------------------------

inline float GGX_D(const Vec3& n, const Vec3& h, float alpha) {
    float NoH = Dot(n, h);
    if (NoH <= 0.0f) return 0.0f;
    float a2 = alpha * alpha;
    float denom = (NoH * NoH * (a2 - 1.0f) + 1.0f);
    return a2 * kInvPi / (denom * denom);
}

inline float GGX_G1(const Vec3& v, const Vec3& h, const Vec3& n, float alpha) {
    float VoH = Dot(v, h);
    if (VoH <= 0.0f) return 0.0f;  // Microfacet is pointing away from the sensor
    float NoV = std::max(0.0001f, Dot(n, v));
    float a2 = alpha * alpha;
    float denom = NoV + std::sqrt(a2 + (1.0f - a2) * NoV * NoV);
    return (2.0f * NoV) / denom;
}

inline float GGX_G(const Vec3& wo, const Vec3& wi, const Vec3& h, const Vec3& n, float alpha) {
    // Smith's shadowing-masking function is the product of G1 for both view and light directions
    return GGX_G1(wo, h, n, alpha) * GGX_G1(wi, h, n, alpha);
}

inline Vec3 SampleGGX(const Vec3& n, float alpha, RNG& rng) {
    float xi1 = rng.UniformFloat();
    float xi2 = rng.UniformFloat();

    // Map random numbers to a microfacet normal (half-vector)
    float phi = 2.0f * kPi * xi1;
    float cosTheta = std::sqrt((1.0f - xi2) / (1.0f + (alpha * alpha - 1.0f) * xi2));
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

    Vec3 h_local(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);

    // Transform from local tangent space to world space
    ONB uvw;
    uvw.BuildFromW(n);
    return uvw.Local(h_local);
}

// Exact dielectric Fresnel reflectance
inline float FrDielectric(float cosThetaI, float etaI, float etaT) {
    cosThetaI = std::clamp(cosThetaI, -1.0f, 1.0f);

    bool entering = cosThetaI > 0.0f;
    if (!entering) {
        std::swap(etaI, etaT);
        cosThetaI = std::abs(cosThetaI);
    }

    // Snell's Law to find sin(ThetaT)
    float sinThetaI = std::sqrt(std::max(0.0f, 1.0f - cosThetaI * cosThetaI));
    float sinThetaT = (etaI / etaT) * sinThetaI;

    // Total Internal Reflection (TIR)
    if (sinThetaT >= 1.0f) return 1.0f;

    float cosThetaT = std::sqrt(std::max(0.0f, 1.0f - sinThetaT * sinThetaT));

    // Exact Fresnel equations
    float Rparl =
        ((etaT * cosThetaI) - (etaI * cosThetaT)) / ((etaT * cosThetaI) + (etaI * cosThetaT));
    float Rperp =
        ((etaI * cosThetaI) - (etaT * cosThetaT)) / ((etaI * cosThetaI) + (etaT * cosThetaT));

    return (Rparl * Rparl + Rperp * Rperp) / 2.0f;
}

Spectrum EvalBSDF(const Material& mat, const ShadingData& sd, const Vec3& wo, const Vec3& wi,
                  const SampledWavelengths& wl) {
    (void)wo;
    if (mat.type != MaterialType::Lambertian) return Spectrum(0.0f);  // specular = Dirac delta

    float cosine = Dot(wi, sd.n_shading);
    if (cosine <= 0.0f) return Spectrum(0.f);

    Spectrum albedo = CurveToSpectrum(sd.albedo, wl);

    return albedo * kInvPi;
}

float PdfBSDF(const Material& mat, const ShadingData& sd, const Vec3& wo, const Vec3& wi) {
    (void)wo;
    if (mat.type != MaterialType::Lambertian) return 0.0f;

    float cosine = Dot(wi, sd.n_shading);
    if (cosine <= 0.0) return 0.f;
    return cosine * kInvPi;  // Cos-weighted hemisphere sampling
}

bool SampleLambertian(const Material& mat, const ShadingData& sd, const SurfaceInteraction& si,
                      RNG& rng, const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f) {
    (void)mat;
    (void)si;
    ONB uvw;
    uvw.BuildFromW(sd.n_shading);

    Vec3 local_dir = RandomCosineDirection(rng);
    wi = uvw.Local(local_dir);

    // Explicit PDF and Eval
    float cosine = std::fmax(0.0f, Dot(wi, sd.n_shading));
    if (cosine <= 0.0f) return false;

    Spectrum albedo = CurveToSpectrum(sd.albedo, wl);
    pdf = cosine / kPi;
    f = albedo * kInvPi;
    return true;
}

bool SampleMetal(const Material& mat, const ShadingData& sd, const SurfaceInteraction& si, RNG& rng,
                 const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f) {
    // Perceptual roughness mapping (artists prefer roughness^2)
    // Clamp to prevent dividing by zero on perfectly smooth mirrors
    float alpha = std::max(0.001f, mat.roughness * mat.roughness);

    Vec3 wo = si.wo;

    // Sample random microscopic mirror normal (half-vector 'h')
    Vec3 h = SampleGGX(sd.n_shading, alpha, rng);

    // Reflect the camera ray off that specific micro-mirror to get the light direction
    wi = Reflect(-wo, h);

    float NoI = Dot(sd.n_shading, wi);
    float NoO = Dot(sd.n_shading, wo);

    if (NoI <= 0.0f || NoO <= 0.0f) return false;  // under surface = kill

    // Evaluate the GGX terms
    float D = GGX_D(sd.n_shading, h, alpha);
    float G = GGX_G(wo, wi, h, sd.n_shading, alpha);

    // F (Fresnel) is handled by the spectral albedo curve for basic metals
    Spectrum F = CurveToSpectrum(mat.albedo, wl);

    // Calculate the PDF of sampling this specific direction
    float HoO = std::abs(Dot(h, wo));
    pdf = (D * Dot(sd.n_shading, h)) / (4.0f * HoO);
    if (pdf <= 0.0f) return false;

    // Assemble the Cook-Torrance Microfacet BRDF
    // f = (D * G * F) / (4 * NoI * NoO)
    f = F * ((D * G) / (4.0f * NoI * NoO));

    return true;
}

// Returns true if a valid bounce occurred, outputs the new direction (wi), pdf, and BSDF (f)
bool SampleDielectric(const Material& mat, const ShadingData& sd, const SurfaceInteraction& si,
                      RNG& rng, const SampledWavelengths& wl, Vec3& wi, float& pdf, Spectrum& f) {
    (void)sd;
    bool entering = si.front_face;
    bool is_dispersive = mat.dispersion > 0.0f;

    // Evaluating Cauchy's IOR for all wavelengths
    Spectrum ior;
    for (int i = 0; i < kNSamples; ++i) {
        if (is_dispersive) {
            // Cauchy expects lambda in micrometers
            float lambda_um = wl.lambda[i] / 1000.0f;
            ior[i] = mat.ior + (mat.dispersion / (lambda_um * lambda_um));
        } else {
            ior[i] = mat.ior;
        }
    }

    // Because intersector flips si.n_geom to face the ray, Dot(wo, n_geom) is always positive
    float absCosI = std::abs(Dot(si.wo, si.n_geom));

    // Fresnel reflectance for all wavelengths
    Spectrum F;
    Spectrum etaI(1.0f);  // Assuming air outside
    Spectrum etaT = ior;
    for (int i = 0; i < kNSamples; ++i) {
        // FrDielectric mathematically requires a negative cosine to know it is exiting
        float cosForFresnel = entering ? absCosI : -absCosI;
        F[i] = FrDielectric(cosForFresnel, etaI[i], etaT[i]);
    }

    // Probability of reflect/refract based on the Hero Wavelength's Fresnel value
    float F_hero = F[0];
    float pr = F_hero;         // Probability to reflect
    float pt = 1.0f - F_hero;  // Probability to refract

    if (rng.UniformFloat() < pr) {
        // Reflection
        // Geometry is same for all wavelengths (Angle In = Angle Out) so we dont kill
        wi = Reflect(-si.wo, si.n_geom);
        pdf = pr;

        // Return BSDF: F / cosTheta (Cosine will be cancelled out in the integrator)
        float absCos = std::abs(Dot(wi, si.n_geom));
        for (int i = 0; i < kNSamples; ++i) {
            f[i] = F[i] / absCos;
        }
        return true;
    } else {
        // Refraction
        float eta_hero_I = entering ? 1.0f : ior[0];
        float eta_hero_T = entering ? ior[0] : 1.0f;
        float eta_hero = eta_hero_I / eta_hero_T;

        // Calculate Snell's law direction using the Hero IOR
        float sin2I = std::max(0.0f, 1.0f - absCosI * absCosI);
        float sin2T = eta_hero * eta_hero * sin2I;

        if (sin2T >= 1.0f) return false;  // Total Internal Reflection catch-all
        float cosT = std::sqrt(1.0f - sin2T);

        wi = -eta_hero * si.wo + (eta_hero * absCosI - cosT) * si.n_geom;
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
}

bool SampleBSDF(const Material& mat, const ShadingData& sd, const Ray& r_in,
                const SurfaceInteraction& si, RNG& rng, const SampledWavelengths& wl, Vec3& wi,
                float& pdf, Spectrum& f) {
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
