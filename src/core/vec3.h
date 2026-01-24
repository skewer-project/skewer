#ifndef VEC3_H
#define VEC3_H

class vec3
{
public:
    double e[3];

    // constructor
    vec3() : e{0, 0, 0} {}
    vec3(double e0, double e1, double e2) : e{e0, e1, e2} {}

    // member functions
    double x() const { return e[0]; }
    double y() const { return e[1]; }
    double z() const { return e[2]; }

    // operator overloads
    vec3 operator-() const { return vec3(-e[0], -e[1], -e[2]); }
    double operator[](int i) const { return e[i]; }
    double &operator[](int i) { return e[i]; }

    vec3 &operator+=(const vec3 &v)
    {
        e[0] += v.e[0];
        e[1] += v.e[1];
        e[2] += v.e[2];
        return *this;
    }

    vec3 &operator*=(double t)
    {
        e[0] *= t;
        e[1] *= t;
        e[2] *= t;
        return *this;
    }

    vec3 &operator/=(double t)
    {
        return *this *= 1 / t;
    }

    // Utility Member Functions

    double length() const
    {
        return std::sqrt(length_squared());
    }

    double length_squared() const
    {
        return e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    }

    // Return true if the vector is close to zero in all dimensions
    bool near_zero() const
    {
        auto s = 1e-8;
        return (std::fabs(e[0]) < s) && (std::fabs(e[1]) < s) && (std::fabs(e[2]) < s);
    }
    // Generating arbitrary random vectors
    static vec3 random()
    {
        return vec3(random_double(), random_double(), random_double());
    }

    static vec3 random(double min, double max)
    {
        return vec3(random_double(min, max), random_double(min, max), random_double(min, max));
    }
};

// point alias for vec3
using point3 = vec3;

// Vector Utility Functions
inline std::ostream &operator<<(std::ostream &out, const vec3 &v)
{
    return out << v.e[0] << ' ' << v.e[1] << ' ' << v.e[2];
}

inline vec3 operator+(const vec3 &u, const vec3 &v)
{
    return vec3(u.e[0] + v.e[0], u.e[1] + v.e[1], u.e[2] + v.e[2]);
}

inline vec3 operator-(const vec3 &u, const vec3 &v)
{
    return vec3(u.e[0] - v.e[0], u.e[1] - v.e[1], u.e[2] - v.e[2]);
}

inline vec3 operator*(const vec3 &u, const vec3 &v)
{
    return vec3(u.e[0] * v.e[0], u.e[1] * v.e[1], u.e[2] * v.e[2]);
}

inline vec3 operator*(double t, const vec3 &v)
{
    return vec3(t * v.e[0], t * v.e[1], t * v.e[2]);
}

inline vec3 operator*(const vec3 &v, double t)
{
    return t * v;
}

inline vec3 operator/(const vec3 &v, double t)
{
    return (1 / t) * v;
}

inline double dot(const vec3 &u, const vec3 &v)
{
    return u.e[0] * v.e[0] + u.e[1] * v.e[1] + u.e[2] * v.e[2];
}

inline vec3 cross(const vec3 &u, const vec3 &v)
{
    return vec3(u.e[1] * v.e[2] - u.e[2] * v.e[1],
                u.e[2] * v.e[0] - u.e[0] * v.e[2],
                u.e[0] * v.e[1] - u.e[1] * v.e[0]);
}

inline vec3 unit_vector(const vec3 &v)
{
    return v / v.length();
}

// Rejection method for generating random vector on surface of a unit sphere (simple but inefficient)
inline vec3 random_unit_vector()
{
    while (true)
    {
        auto p = vec3::random(-1, 1);
        auto lensq = p.length_squared();
        // Add lower bound to avoid underflow error (small values -> 0 near center of sphere)
        if (1e-160 < lensq && lensq <= 1)
        { // normalize to produce unit vector if it's within unit sphere
            return p / sqrt(lensq);
        }
    }
}

// Check if unit vector is on the same hemisphere as normal (want it pointing away from surface)
inline vec3 random_on_hemisphere(const vec3 &normal)
{
    vec3 generated_unit_vec = random_unit_vector();
    if (dot(generated_unit_vec, normal) > 0.0) // aligned with normal
        return generated_unit_vec;
    else
        return -generated_unit_vec;
}

// A ray v coming in down-right with a normal n pointing straight up hits the surface,
// the downward force must reflect while the sideways motion remains constant.
// We isolate downward force by projecting v onto n (b)
// b = (v dot n) * n     <-   dot results in scalar so multiply by n for downward dir
// -b just negates downward motion, so -2b reflects it opposite way
inline vec3 reflect(const vec3 &v, const vec3 &n)
{
    return v - 2 * dot(v, n) * n;
}

// Based on snells law: eta * sin(theta) = eta' * sin(theta')
// refrated ray = sin(theta') = eta/eta' * sin(theta)
// On refracted side of surface, there's refracted ray R' and normal n', with angle theta'
// R' can be split into perpendicular R' and parallel R' (to the normal n')
// messy final equation but treated as fact for now
inline vec3 refract(const vec3 &uv, const vec3 &n, double etai_over_etat)
{
    auto cos_theta = std::fmin(dot(-uv, n), 1.0);
    vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
    vec3 r_out_parallel = -std::sqrt(std::fabs(1.0 - r_out_perp.length_squared())) * n;
    return r_out_perp + r_out_parallel;
}

// Defocus disk
inline vec3 random_in_unit_disk()
{
    while (true)
    {
        auto p = vec3(random_double(-1, 1), random_double(-1, 1), 0);
        if (p.length_squared() < 1)
            return p;
    }
}

#endif