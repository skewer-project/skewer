#ifndef SKWR_CORE_CPU_CONFIG_H_
#define SKWR_CORE_CPU_CONFIG_H_

#include <cstdint>

/**
 * Number of wavelengths to sample at a time, each raycast
 * Includes the hero wavelength
 */
constexpr int kNSamples = 4;

/**
 * Define a maximum overlap depth. 4 is the industry standard for path tracers
 * (e.g., camera -> glass -> water -> ice). Exceeding 4 overlaps of transmissive
 * media is exceptionally rare and usually indicates bad geometry.
 */
constexpr uint8_t kMaxMediumStack = 4;

/**
 * Opaque ID for a vacuum/empty space
 */
constexpr uint16_t kVacuumMediumId = 0xFFFF;

#endif  // SKWR_CORE_CPU_CONFIG_H_
