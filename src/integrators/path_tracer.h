#ifndef PATH_TRACER_H
#define PATH_TRACER_H

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

// Can include usings, but commented out for clarity
// using std::make_shared;
// using std::shared_ptr;

// Constants
const double infinity = std::numeric_limits<double>::infinity();
const double pi = 3.1415926535897932385;

// Utility Functions
inline double degrees_to_radians(double degrees)
{
    return degrees * pi / 180.0;
}

inline double random_double()
{
    // Return a random real number in [0,1)
    return std::rand() / (RAND_MAX + 1.0);
}

inline double random_double(double min, double max)
{
    // Return a random real number in [min,max)
    return min + (max - min) * random_double();
}

/* Alternate implementation with <random> header */
// #include <random>
// inline double random_double() {
//     static std::uniform_real_distribution<double> distribution(0.0, 1.0);
//     static std::mt19937 generator;
//     return distribution(generator);
// }

// Common Headers
#include "core/spectrum.h"
#include "core/interval.h"
#include "core/ray.h"
#include "core/vec3.h"

#endif