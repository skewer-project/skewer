#ifndef SKWR_KERNELS_SAMPLE_HOMOGENEOUS_H_
#define SKWR_KERNELS_SAMPLE_HOMOGENEOUS_H_

#include <algorithm>
#include <cmath>

#include "core/cpu_config.h"
#include "core/math/constants.h"
#include "core/ray.h"
#include "core/sampling/medium_interaction.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectrum.h"
#include "media/mediums.h"

namespace skwr {

/**
 * In a homogeneous medium, density (σ_t) is constant, so the probability of a photon traveling a
 * distance t without hitting a particle is given by the transmittance (Beer-Lambert law):
 *      T_r(t) = e^(−σ_t * t)
 * For us, σ_t is a Spectrum, not a single float. A ray might travel further in the red channel than
 * the blue channel. So to prevent color bias, we randomly pick one color channel to generate the
 * distance t, and then average the Probability Density Function (PDF) across all channels to update
 * the throughput (β) beta
 *
 * To sample a random scattering distance t, we use the Inverse Transform Method.
 * Set a random number ξ equal to the cumulative distribution function and solve for t:
 *      t = −ln(1 − ξ) / σ_t
 */
inline bool SampleHomogeneous(const HomogeneousMedium& medium, const Ray& r, float t_max, RNG& rng,
                              Spectrum& beta, MediumInteraction& mi) {
    Spectrum sigma_t = medium.Extinction();  // sigma_a + sigma_s

    // Sampling a color channel to find t
    int channel = rng.UniformInt(kNSamples);
    float sigma_t_c = sigma_t[channel];

    // Solve for t
    float xi = rng.UniformFloat();

    // Protect against division by zero for perfectly clear media
    float t = kInfinity;
    if (sigma_t_c > 0.0f) {
        t = -std::log(std::max(1.0f - xi, kEpsilon)) / sigma_t_c;
    }

    // Determine if hit a particle OR passed through to the surface
    bool scattered = (t < t_max);
    float t_eval = scattered ? t : t_max;

    // Solve for transmittance, Beer-Lambert law
    Spectrum tr;
    for (int i = 0; i < kNSamples; ++i) {
        tr[i] = std::exp(-sigma_t[i] * t_eval);
    }

    // If scattered: PDF = sigma_t * T_r
    // If passed through: PDF = T_r
    Spectrum pdf_spectrum = scattered ? (sigma_t * tr) : tr;
    float pdf = pdf_spectrum.Average();

    if (pdf <= 0.0f) return false;  // Maybe pdf < 1e-6f: be aware of dividing float by small nums

    // Update beta
    // Beta modifies the ray's carrying capacity: beta *= (Transmittance * Scattering) / PDF
    if (scattered) {
        beta *= (tr * medium.sigma_s) / pdf;

        mi.t = t;
        mi.point = r.at(t);
        mi.wo = -r.direction();  // Points back towards the camera/previous bounce
        mi.phase_g = medium.g;
        mi.sigma_s = medium.sigma_s;
        mi.alpha = 1.0f;  // For deep buffer: represents a hard volumetric collision

        return true;
    } else {
        // Ray survived the volume and reached the surface
        beta *= tr / pdf;
        return false;
    }

    return true;
}

}  // namespace skwr

#endif  // SKWR_KERNELS_SAMPLE_HOMOGENEOUS_H_
