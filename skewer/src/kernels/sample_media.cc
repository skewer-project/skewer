#include "kernels/sample_media.h"

#include <algorithm>
#include <cmath>

#include "core/cpu_config.h"
#include "core/math/constants.h"
#include "core/ray.h"
#include "core/sampling/rng.h"
#include "core/spectral/spectrum.h"
#include "core/transport/medium_interaction.h"
#include "geometry/boundbox.h"
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
bool SampleHomogeneous(const HomogeneousMedium& medium, const Ray& r, float t_max, RNG& rng,
                       Spectrum& beta, MediumInteraction* mi) {
    Spectrum sigma_t = medium.Extinction();  // sigma_a + sigma_s

    // Sampling a color channel to find t
    int channel = rng.UniformInt(kNSamples);
    float sigma_t_c = sigma_t[channel];

    // Solve for t
    float xi = rng.UniformFloat();

    // Protect against division by zero for perfectly clear media
    float t = kInfinity;
    if (sigma_t_c > 0.0f) {
        t = -std::log(std::max(1.0f - xi, kFloatEpsilon)) / sigma_t_c;
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

        mi->t = t;
        mi->point = r.at(t);
        mi->wo = -r.direction();  // Points back towards the camera/previous bounce
        mi->phase_g = medium.g;
        mi->sigma_s = medium.sigma_s;
        mi->alpha = 1.0f - tr.Average();

        return true;
    } else {
        // Ray survived the volume and reached the surface
        beta *= tr / pdf;
        return false;
    }
}

/**
 * In heterogeneous media, we take the max density as a majorant (upper bound) to sample as if it
 * were homogeneous, but for areas with density < majorant, we take a probability of the sample,
 * corresponding to the reduced density
 */
bool SampleGrid(const GridMedium& medium, const Ray& r, float t_max_surface, RNG& rng,
                Spectrum& beta, MediumInteraction* mi) {
    float t_min_box = 0.0f;
    float t_max_box = kInfinity;
    if (!medium.bbox.IntersectP(r, t_min_box, t_max_box)) return false;

    // Constrain marching bounds
    float t_min = std::max(0.0f, t_min_box);  // start at the box edge or where ray currently is
    float t_max = std::min(t_max_surface, t_max_box);  // stop at the box edge or at solid surface
    if (t_min >= t_max) return false;

    // Delta Tracking (Woodcock Tracking) Loop
    float t = t_min;
    float majorant =
        medium.max_density * (medium.sigma_a_base + medium.sigma_s_base).MaxComponentValue();
    if (majorant <= 0.0f) return false;
    int hero = 0;  // 0 index is always hero

    // Track optical depth for Deep Alpha
    float accumulated_tau = 0.0f;

    while (true) {
        // Sample a distance step based on the majorant
        float xi_1 = rng.UniformFloat();
        float step_size = -std::log(std::max(1.0f - xi_1, kFloatEpsilon)) / majorant;
        t += step_size;

        if (t >= t_max) break;  // Exited vol if stepped out of the box or hit surface

        // The REAL density at this point
        float density = medium.GetDensity(r.at(t));
        // Calculate the actual Extinction coefficient here
        Spectrum sigma_t = density * (medium.sigma_a_base + medium.sigma_s_base);
        Spectrum sigma_s = density * medium.sigma_s_base;

        // Accumulate optical depth (tau)
        // approximating the integral of extinction over the distance marched.
        accumulated_tau += sigma_t.Average() * step_size;

        // Probability of a REAL collision
        float p_real = sigma_t[hero] / majorant;

        if (rng.UniformFloat() < p_real) {
            // --- REAL COLLISION! ---
            // Divide the entire spectrum by the HERO's extinction
            beta *= (sigma_s / sigma_t[hero]);

            mi->t = t;
            mi->point = r.at(t);
            mi->wo = -r.direction();
            mi->phase_g = medium.g;
            mi->sigma_s = sigma_s;
            mi->alpha = 1.0f - std::exp(-accumulated_tau);

            return true;
        }

        // --- NULL COLLISION ---
        // If the grid has spectrally varying extinction (e.g., colored base_sigma_t),
        // we must weight the non-hero channels.
        // Note: If sigma_t is perfectly uniform across all channels (grey smoke),
        // this safely evaluates to 1.0.
        float denom = std::max(majorant - sigma_t[hero], kFloatEpsilon);
        Spectrum null_weight = (Spectrum(majorant) - sigma_t) / denom;
        beta *= null_weight;
    }
    return false;
}

}  // namespace skwr
