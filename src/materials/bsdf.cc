#include "materials/bsdf.h"

#include "core/constants.h"
#include "core/onb.h"
#include "core/sampling.h"
#include "core/spectral/spectral_utils.h"

namespace skwr {

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

Spectrum EvalBSDF(const Material& mat, const Vec3& wo, const Vec3& wi, const Vec3& n,
                  const SampledWavelengths& wl) {
    if (mat.type != MaterialType::Lambertian) return Spectrum(0.0f);  // specular = Dirac delta

    float cosine = Dot(wi, n);
    if (cosine <= 0.0f) return Spectrum(0.f);

    Spectrum albedo = CurveToSpectrum(mat.albedo, wl);

    return albedo * (1.0f / kPi);
}

float PdfBSDF(const Material& mat, const Vec3& wo, const Vec3& wi, const Vec3 n) {
    if (mat.type != MaterialType::Lambertian) return 0.f;

    float cosine = Dot(wi, n);
    if (cosine <= 0) return 0.f;
    return cosine * (1.0f / kPi);  // Cos-weighted sampling
}

bool SampleLambertian(const Material& mat, const SurfaceInteraction& si, RNG& rng, Vec3& wi,
                      float& pdf, Spectrum& f) {
    ONB uvw;
    uvw.BuildFromW(si.n_geom);

    Vec3 local_dir = RandomCosineDirection(rng);
    wi = uvw.Local(local_dir);

    // Explicit PDF and Eval
    float cosine = std::fmax(0.0f, Dot(wi, si.n_geom));
    pdf = cosine / kPi;
    f = mat.albedo * (1.0f / kPi);
    return true;
}

bool SampleMetal(const Material& mat, const SurfaceInteraction& si, RNG& rng, Vec3& wi, float& pdf,
                 Spectrum& f) {
    wi = Reflect(-si.wo, si.n_geom);  // We reflect "incoming view" = -wo
    if (mat.roughness > 0) {
        wi = Normalize(wi + (mat.roughness * RandomInUnitSphere(rng)));
    }

    // Check if valid (above surface)
    float cosine = Dot(wi, si.n_geom);
    if (cosine <= 0) return false;

    // Delta Distribution Logic
    pdf = 1.0f;
    f = mat.albedo / cosine;  // Cancels the cosine in the rendering equation
    return true;
}

// Returns true if a valid bounce occurred, outputs the new direction (wi), pdf, and BSDF (f)
inline bool SampleDielectric(const Material& mat, const Ray& r_in, const SurfaceInteraction& si,
                             RNG& rng, const SampledWavelengths& wl, Vec3& wi_out, float& pdf_out,
                             Spectrum& f_out) {
    Vec3 wo = -r_in.direction();  // View vector
    float cosThetaI = Dot(wo, si.n_geom);
    bool entering = cosThetaI > 0.0f;
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

    // Fresnel reflectance for all wavelengths
    Spectrum F;
    Spectrum etaI(1.0f);  // Assuming air outside
    Spectrum etaT = ior;
    for (int i = 0; i < kNSamples; ++i) {
        F[i] = FrDielectric(cosThetaI, etaI[i], etaT[i]);
    }

    // Probability of reflect/refract based on the Hero Wavelength's Fresnel value
    float F_hero = F[0];
    float pr = F_hero;         // Probability to reflect
    float pt = 1.0f - F_hero;  // Probability to refract

    if (rng.UniformFloat() < pr) {
        // ---- REFLECTION ----
        // Reflection geometry is identical for all wavelengths (Angle In = Angle Out).
        // Therefore, we DO NOT terminate companion wavelengths.
        wi_out = Reflect(wo, si.n_geom);
        pdf_out = pr;

        // Return BSDF: F / cosTheta (Cosine will be cancelled out in the integrator)
        float absCos = std::abs(Dot(wi_out, si.n_geom));
        for (int i = 0; i < kNSamples; ++i) {
            f_out[i] = F[i] / absCos;
        }
        return true;

    } else {
        // ---- REFRACTION ----
        float eta_hero_I = entering ? 1.0f : ior[0];
        float eta_hero_T = entering ? ior[0] : 1.0f;
        float eta_hero = eta_hero_I / eta_hero_T;

        // Calculate Snell's law direction using the Hero IOR
        Vec3 n = entering ? si.n_geom : -si.n_geom;
        float cosI = std::abs(cosThetaI);
        float sin2I = std::max(0.0f, 1.0f - cosI * cosI);
        float sin2T = eta_hero * eta_hero * sin2I;

        if (sin2T >= 1.0f) return false;  // Total Internal Reflection catch-all
        float cosT = std::sqrt(1.0f - sin2T);

        wi_out = eta_hero * (-wo) + (eta_hero * cosI - cosT) * n;
        pdf_out = pt;

        float absCos = std::abs(Dot(wi_out, si.n_geom));
        f_out = Spectrum(0.0f);  // Initialize all to black

        if (is_dispersive) {
            // HERO TERMINATION RULE:
            // Because the material bends wavelengths differently, the companions
            // would have missed this path. Leaving them at 0.0f removes variance
            f_out[0] = (1.0f - F[0]) / absCos;
        } else {
            // Constant IOR: All wavelengths follow this exact path. Keep them all.
            for (int i = 0; i < kNSamples; ++i) {
                f_out[i] = (1.0f - F[i]) / absCos;
            }
        }
        return true;
    }
}

bool SampleBSDF(const Material& mat, const Ray& r_in, const SurfaceInteraction& si, RNG& rng,
                Vec3& wi, float& pdf, Spectrum& f) {
    switch (mat.type) {
        case MaterialType::Lambertian:
            return SampleLambertian(mat, si, rng, wi, pdf, f);

        case MaterialType::Metal:
            return SampleMetal(mat, si, rng, wi, pdf, f);

        case MaterialType::Dielectric:
            return SampleDielectric(mat, si, rng, wi, pdf, f);  // Implement similarly
    }
    return false;
}

}  // namespace skwr
