#ifndef INTERVAL_H
#define INTERVAL_H

#include "integrators/path_tracer.h"

class interval {
  public:
    double min, max;

    interval() : min(+infinity), max(-infinity) {}  // Default interval is empty

    interval(double min, double max) : min(min), max(max) {}

    // Constructor to create interval containing two intervals (union)
    interval(const interval& a, const interval& b) {
        min = a.min <= b.min ? a.min : b.min;
        max = a.max >= b.max ? a.max : b.max;
    }

    double size() const { return max - min; }

    bool contains(double x) const { return min <= x && x <= max; }

    bool surrounds(double x) const { return min < x && x < max; }

    // Helper function to ensure color components of sampling remain within [0,1]
    double clamp(double x) const {
        if (x < min) return min;
        if (x > max) return max;
        return x;
    }

    // Expand the interval by delta on each side
    interval expand(double delta) const {
        auto padding = delta / 2;
        return interval(min - padding, max + padding);
    }

    static const interval empty, universe;
};

const interval interval::empty = interval(+infinity, -infinity);
const interval interval::universe = interval(-infinity, +infinity);

// Offset interval by displacement
inline interval operator+(const interval& ival, double displacement) {
    return interval(ival.min + displacement, ival.max + displacement);
}

inline interval operator+(double displacement, const interval& ival) { return ival + displacement; }

#endif
