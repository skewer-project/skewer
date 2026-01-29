#ifndef PATH_TRACER_H
#define PATH_TRACER_H
//==============================================================================================
// Common includes, constants, and utility functions for the ray tracer.
//==============================================================================================

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <thread>

// C++ Std Usings
using std::make_shared;
using std::shared_ptr;

// Constants
const double infinity = std::numeric_limits<double>::infinity();
const double pi = 3.1415926535897932385;

// Thread-safe random number generator
// Each thread gets its own generator, seeded uniquely using random_device + thread id
inline std::mt19937& get_thread_local_generator() {
    static thread_local std::mt19937 generator = []() {
        std::random_device rd;
        auto seed = rd() ^ (std::hash<std::thread::id>{}(std::this_thread::get_id()));
        return std::mt19937(seed);
    }();
    return generator;
}

// Utility Functions
inline double degrees_to_radians(double degrees) { return degrees * pi / 180.0; }

inline double random_double() {
    // Returns a random real in [0,1).
    static thread_local std::uniform_real_distribution<double> distribution(0.0, 1.0);
    return distribution(get_thread_local_generator());
}

inline double random_double(double min, double max) {
    // Returns a random real in [min,max).
    std::uniform_real_distribution<double> distribution(min, max);
    return distribution(get_thread_local_generator());
}

inline int random_int(int min, int max) {
    // Returns a random integer in [min,max].
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(get_thread_local_generator());
}

#endif
