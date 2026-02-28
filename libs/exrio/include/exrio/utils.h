#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace exrio {

/**
 * Global verbosity flag for logging
 */
extern bool g_verbose;

/**
 * Set the global verbosity level
 */
void setVerbose(bool verbose);

/**
 * Check if verbose mode is enabled
 */
bool isVerbose();

/**
 * Log a message (only in verbose mode)
 */
void logVerbose(const std::string& message);

/**
 * Log a message (always)
 */
void log(const std::string& message);

/**
 * Log an error message
 */
void logError(const std::string& message);

/**
 * Simple timer class for performance measurements
 */
class Timer {
  public:
    Timer();

    /**
     * Reset the timer
     */
    void reset();

    /**
     * Get elapsed time in milliseconds
     */
    double elapsedMs() const;

    /**
     * Get elapsed time as a formatted string
     */
    std::string elapsedString() const;

  private:
    std::chrono::high_resolution_clock::time_point start_;
};

/**
 * Format a number with commas for readability
 */
std::string formatNumber(size_t number);

/**
 * Format bytes as human-readable string
 */
std::string formatBytes(size_t bytes);

/**
 * Get the filename from a path
 */
std::string getFilename(const std::string& path);

/**
 * Get the directory from a path
 */
std::string getDirectory(const std::string& path);

/**
 * Check if a file exists
 */
bool fileExists(const std::string& path);

/**
 * Clamp a value between min and max
 */
template <typename T>
T clamp(T value, T minVal, T maxVal) {
    return std::max(minVal, std::min(maxVal, value));
}

/**
 * Linear interpolation
 */
template <typename T>
T lerp(T a, T b, float t) {
    return a + (b - a) * t;
}

}  // namespace exrio
