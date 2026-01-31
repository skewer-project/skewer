#ifndef SKWR_CORE_SPECTRUM_H_
#define SKWR_CORE_SPECTRUM_H_

#include "core/color3f.h"

// Spectrum is an abstraction for color3f.
// Spectrum represents the distribution of light energy (RADIANCE) across wavelengths
// Color3f represents the human-visible RGB perception
//

namespace skwr {

// CONFIGURATION:
// For now, we simulate light using RGB values.
// In the future, we can change this ONE line to switch to Spectral Rendering (8 wavelengths).
using Spectrum = Color3f;

}  // namespace skwr

#endif  // SKWR_CORE_SPECTRUM_H_
