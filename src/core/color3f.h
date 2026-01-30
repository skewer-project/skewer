#ifndef SKWR_CORE_COLOR3F_H_
#define SKWR_CORE_COLOR3F_H_

#include <cmath>
#include <iostream>
#include <algorithm>

namespace skwr
{

    struct Color3f
    {
        float r, g, b;

        // Constructors
        Color3f() : r(0.0f), g(0.0f), b(0.0f) {}
        Color3f(float r, float g, float b) : r(r), g(g), b(b) {}
        Color3f(double r, double g, double b) : r(static_cast<float>(r)), g(static_cast<float>(g)), b(static_cast<float>(b)) {}
        Color3f(int r, int g, int b) : r(static_cast<float>(r)), g(static_cast<float>(g)), b(static_cast<float>(b)) {}

        float operator[](int i) const
        {
            if (i == 0)
                return r;
            if (i == 1)
                return g;
            return b;
        }

        // reference version for assignment
        float &operator[](int i)
        {
            if (i == 0)
                return r;
            if (i == 1)
                return g;
            return b;
        }

        Color3f &operator+=(const Color3f &c)
        {
            r += c.r;
            g += c.g;
            b += c.b;
            return *this;
        }

        Color3f &operator-=(const Color3f &c)
        {
            r -= c.r;
            g -= c.g;
            b -= c.b;
            return *this;
        }

        Color3f &operator*=(const Color3f &c)
        {
            r *= c.r;
            g *= c.g;
            b *= c.b;
            return *this;
        }

        Color3f &operator*=(float t)
        {
            r *= t;
            g *= t;
            b *= t;
            return *this;
        }

        Color3f &operator/=(float t)
        {
            float k = 1.0 / t;
            r *= k;
            g *= k;
            b *= k;
            return *this;
        }

        void applygammacorrection()
        {
            auto lineartogamma = [](float x)
            {
                return (x > 0) ? std::sqrt(x) : 0;
            };
            r = lineartogamma(r);
            g = lineartogamma(g);
            b = lineartogamma(b);
        }

        // Clamp helper
        Color3f clamp(float min = 0.0f, float max = 1.0f) const
        {
            return Color3f(
                std::clamp(r, min, max),
                std::clamp(g, min, max),
                std::clamp(b, min, max));
        }
        // Manual version if not C++17
        /*
        Color3f clamped(float min = 0.0f, float max = 1.0f) const {
            auto c = [](float val, float low, float high) {
                return val < low ? low : (val > high ? high : val);
            };
            return Color3f(c(r, min, max), c(g, min, max), c(b, min, max));
        }
        */
    };

    inline std::ostream &operator<<(std::ostream &out, const Color3f &c)
    {
        return out << c.r << ' ' << c.g << ' ' << c.b;
    }

    inline Color3f operator+(const Color3f &c, const Color3f &d)
    {
        return Color3f(c.r + d.r, c.g + d.g, c.b + d.b);
    }

    inline Color3f operator-(const Color3f &c, const Color3f &d)
    {
        return Color3f(c.r - d.r, c.g - d.g, c.b - d.b);
    }

    inline Color3f operator*(const Color3f &c, const Color3f &d)
    {
        return Color3f(c.r * d.r, c.g * d.g, c.b * d.b);
    }

    inline Color3f operator*(float t, const Color3f &c)
    {
        return Color3f(t * c.r, t * c.g, t * c.b);
    }

    inline Color3f operator*(const Color3f &c, float t)
    {
        return t * c;
    }

    inline Color3f operator/(const Color3f &c, float t)
    {
        return c * (1.0 / t);
    }

    // void write_color(std::ostream &out, const Color3f &pixel_color)
    // {
    //     if (std::isnan(pixel_color.r))
    //         return;

    //     // Copy so we can gamma correct
    //     Color3f c = pixel_color;

    //     c.applygammacorrection();

    //     // Translate [0,1] component values to rgb range [0,255]
    //     c.clamp(0.0f, 0.999f);

    //     // since adding average of all samples, need to clamp values to prevent going
    //     // beyond [0,1] range

    //     // Write out pixel components
    //     out << static_cast<int>(256 * c.r) << ' ' << static_cast<int>(256 * c.g) << ' ' << static_cast<int>(256 * c.b) << '\n';
    // }
} // namespace skwr

#endif // SKWR_CORE_COLOR3F_H_