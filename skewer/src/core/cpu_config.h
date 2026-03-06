#ifndef SKWR_CORE_CPU_CONFIG_H_
#define SKWR_CORE_CPU_CONFIG_H_

#include <cstdint>

/**
 * Number of wavelengths to sample at a time, each raycast
 * Includes the hero wavelength
 */
constexpr int kNSamples = 4;

/** Hardcap based on maximum allowed bounces (e.g., 16 or 32) */
constexpr size_t kMaxDeepSegments = 16;

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
constexpr uint16_t kNullMaterialId = 0xFFFF;  // For semantic difference

#endif  // SKWR_CORE_CPU_CONFIG_H_
