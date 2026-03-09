#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace deep_compositor {

/**
 * Global verbosity flag for logging
 */
extern bool g_verbose;

/**
 * Set the global verbosity level
 */
void SetVerbose(bool verbose);

/**
 * Check if verbose mode is enabled
 */
bool IsVerbose();

/**
 * Log a message (only in verbose mode)
 */
void LogVerbose(const std::string& message);

/**
 * Log a message (always)
 */
void Log(const std::string& message);

/**
 * Log an error message
 */
void LogError(const std::string& message);

/**
 * Simple timer class for performance measurements
 */
class Timer {
  public:
    Timer();

    /**
     * Reset the timer
     */
    void Reset();

    /**
     * Get elapsed time in milliseconds
     */
    double ElapsedMs() const;

    /**
     * Get elapsed time as a formatted string
     */
    std::string ElapsedString() const;

  private:
    std::chrono::high_resolution_clock::time_point start_;
};

/**
 * Format a number with commas for readability
 */
std::string FormatNumber(size_t number);

/**
 * Format bytes as human-readable string
 */
std::string FormatBytes(size_t bytes);

/**
 * Get the filename from a path
 */
std::string GetFilename(const std::string& path);

/**
 * Get the directory from a path
 */
std::string GetDirectory(const std::string& path);

/**
 * Check if a file exists
 */
bool FileExists(const std::string& path);

/**
 * Clamp a value between min and max
 */
template <typename T>
T Clamp(T value, T minVal, T maxVal) {
    return std::max(minVal, std::min(maxVal, value));
}

/**
 * Linear interpolation
 */
template <typename T>
T lerp(T a, T b, float t) {
    return a + (b - a) * t;
}

}  // namespace deep_compositor
