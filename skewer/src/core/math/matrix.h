#ifndef SKWR_CORE_MATH_MATRIX_H_
#define SKWR_CORE_MATH_MATRIX_H_

#include "core/math/vec3.h"
#include "core/math/quaternion.h"
#include <cmath>

namespace skwr {

struct Matrix4 {
    float m[4][4];

    Matrix4() {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                m[i][j] = 0.0f;
    }

    static Matrix4 Identity() {
        Matrix4 mat;
        mat.m[0][0] = mat.m[1][1] = mat.m[2][2] = mat.m[3][3] = 1.0f;
        return mat;
    }

    static Matrix4 Translate(const Vec3& v) {
        Matrix4 mat = Identity();
        mat.m[0][3] = v.x();
        mat.m[1][3] = v.y();
        mat.m[2][3] = v.z();
        return mat;
    }

    static Matrix4 Scale(float s) {
        Matrix4 mat = Identity();
        mat.m[0][0] = mat.m[1][1] = mat.m[2][2] = s;
        return mat;
    }

    static Matrix4 Scale(const Vec3& s) {
        Matrix4 mat = Identity();
        mat.m[0][0] = s.x();
        mat.m[1][1] = s.y();
        mat.m[2][2] = s.z();
        return mat;
    }

    static Matrix4 Rotate(const Quaternion& q) {
        Matrix4 mat = Identity();
        float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
        float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
        float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
        float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;

        mat.m[0][0] = 1.0f - (yy + zz);
        mat.m[0][1] = xy - wz;
        mat.m[0][2] = xz + wy;

        mat.m[1][0] = xy + wz;
        mat.m[1][1] = 1.0f - (xx + zz);
        mat.m[1][2] = yz - wx;

        mat.m[2][0] = xz - wy;
        mat.m[2][1] = yz + wx;
        mat.m[2][2] = 1.0f - (xx + yy);

        return mat;
    }

    Matrix4 operator*(const Matrix4& other) const {
        Matrix4 res;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                res.m[i][j] = m[i][0] * other.m[0][j] +
                             m[i][1] * other.m[1][j] +
                             m[i][2] * other.m[2][j] +
                             m[i][3] * other.m[3][j];
            }
        }
        return res;
    }

    Vec3 TransformPoint(const Vec3& p) const {
        float x = m[0][0] * p.x() + m[0][1] * p.y() + m[0][2] * p.z() + m[0][3];
        float y = m[1][0] * p.x() + m[1][1] * p.y() + m[1][2] * p.z() + m[1][3];
        float z = m[2][0] * p.x() + m[2][1] * p.y() + m[2][2] * p.z() + m[2][3];
        float w = m[3][0] * p.x() + m[3][1] * p.y() + m[3][2] * p.z() + m[3][3];
        if (w != 1.0f && w != 0.0f) {
            return Vec3(x / w, y / w, z / w);
        }
        return Vec3(x, y, z);
    }

    Vec3 TransformVector(const Vec3& v) const {
        float x = m[0][0] * v.x() + m[0][1] * v.y() + m[0][2] * v.z();
        float y = m[1][0] * v.x() + m[1][1] * v.y() + m[1][2] * v.z();
        float z = m[2][0] * v.x() + m[2][1] * v.y() + m[2][2] * v.z();
        return Vec3(x, y, z);
    }

    // Simplified Inverse for SRT matrices (Inverse = S^-1 * R^-1 * T^-1)
    // For now, implement a more general but still efficient version if possible.
    // Given the roadmap, we need M^-1(t).
    Matrix4 Inverse() const {
        // Standard 4x4 Gauss-Jordan or cofactor inverse could be overkill if we only use SRT.
        // But for generality, let's implement a reasonably robust one.
        float inv[16];
        const float* mat = &m[0][0];

        inv[0] = mat[5]  * mat[10] * mat[15] - 
                 mat[5]  * mat[11] * mat[14] - 
                 mat[9]  * mat[6]  * mat[15] + 
                 mat[9]  * mat[7]  * mat[14] +
                 mat[13] * mat[6]  * mat[11] - 
                 mat[13] * mat[7]  * mat[10];

        inv[4] = -mat[4]  * mat[10] * mat[15] + 
                  mat[4]  * mat[11] * mat[14] + 
                  mat[8]  * mat[6]  * mat[15] - 
                  mat[8]  * mat[7]  * mat[14] - 
                  mat[12] * mat[6]  * mat[11] + 
                  mat[12] * mat[7]  * mat[10];

        inv[8] = mat[4]  * mat[9] * mat[15] - 
                 mat[4]  * mat[11] * mat[13] - 
                 mat[8]  * mat[5] * mat[15] + 
                 mat[8]  * mat[7] * mat[13] + 
                 mat[12] * mat[5] * mat[11] - 
                 mat[12] * mat[7] * mat[9];

        inv[12] = -mat[4]  * mat[9] * mat[14] + 
                   mat[4]  * mat[10] * mat[13] +
                   mat[8]  * mat[5] * mat[14] - 
                   mat[8]  * mat[6] * mat[13] - 
                   mat[12] * mat[5] * mat[10] + 
                   mat[12] * mat[6] * mat[9];

        inv[1] = -mat[1]  * mat[10] * mat[15] + 
                  mat[1]  * mat[11] * mat[14] + 
                  mat[9]  * mat[2] * mat[15] - 
                  mat[9]  * mat[3] * mat[14] - 
                  mat[13] * mat[2] * mat[11] + 
                  mat[13] * mat[3] * mat[10];

        inv[5] = mat[0]  * mat[10] * mat[15] - 
                 mat[0]  * mat[11] * mat[14] - 
                 mat[8]  * mat[2] * mat[15] + 
                 mat[8]  * mat[3] * mat[14] + 
                 mat[12] * mat[2] * mat[11] - 
                 mat[12] * mat[3] * mat[10];

        inv[9] = -mat[0]  * mat[9] * mat[15] + 
                  mat[0]  * mat[11] * mat[13] + 
                  mat[8]  * mat[1] * mat[15] - 
                  mat[8]  * mat[3] * mat[13] - 
                  mat[12] * mat[1] * mat[11] + 
                  mat[12] * mat[3] * mat[9];

        inv[13] = mat[0]  * mat[9] * mat[14] - 
                  mat[0]  * mat[10] * mat[13] - 
                  mat[8]  * mat[1] * mat[14] + 
                  mat[8]  * mat[2] * mat[13] + 
                  mat[12] * mat[1] * mat[10] - 
                  mat[12] * mat[2] * mat[9];

        inv[2] = mat[1]  * mat[6] * mat[15] - 
                 mat[1]  * mat[7] * mat[14] - 
                 mat[5]  * mat[2] * mat[15] + 
                 mat[5]  * mat[3] * mat[14] + 
                 mat[13] * mat[2] * mat[7] - 
                 mat[13] * mat[3] * mat[6];

        inv[6] = -mat[0]  * mat[6] * mat[15] + 
                  mat[0]  * mat[7] * mat[14] + 
                  mat[4]  * mat[2] * mat[15] - 
                  mat[4]  * mat[3] * mat[14] - 
                  mat[12] * mat[2] * mat[7] + 
                  mat[12] * mat[3] * mat[6];

        inv[10] = mat[0]  * mat[5] * mat[15] - 
                  mat[0]  * mat[7] * mat[13] - 
                  mat[4]  * mat[1] * mat[15] + 
                  mat[4]  * mat[3] * mat[13] + 
                  mat[12] * mat[1] * mat[7] - 
                  mat[12] * mat[3] * mat[5];

        inv[14] = -mat[0]  * mat[5] * mat[14] + 
                   mat[0]  * mat[6] * mat[13] + 
                   mat[4]  * mat[1] * mat[14] - 
                   mat[4]  * mat[2] * mat[13] - 
                   mat[12] * mat[1] * mat[6] + 
                   mat[12] * mat[2] * mat[5];

        inv[3] = -mat[1] * mat[6] * mat[11] + 
                  mat[1] * mat[7] * mat[10] + 
                  mat[5] * mat[2] * mat[11] - 
                  mat[5] * mat[3] * mat[10] - 
                  mat[9] * mat[2] * mat[7] + 
                  mat[9] * mat[3] * mat[6];

        inv[7] = mat[0] * mat[6] * mat[11] - 
                 mat[0] * mat[7] * mat[10] - 
                 mat[4] * mat[2] * mat[11] + 
                 mat[4] * mat[3] * mat[10] + 
                 mat[8] * mat[2] * mat[7] - 
                 mat[8] * mat[3] * mat[6];

        inv[11] = -mat[0] * mat[5] * mat[11] + 
                   mat[0] * mat[7] * mat[9] + 
                   mat[4] * mat[1] * mat[11] - 
                   mat[4] * mat[3] * mat[9] - 
                   mat[8] * mat[1] * mat[7] + 
                   mat[8] * mat[3] * mat[5];

        inv[15] = mat[0] * mat[5] * mat[10] - 
                  mat[0] * mat[6] * mat[9] - 
                  mat[4] * mat[1] * mat[10] + 
                  mat[4] * mat[2] * mat[9] + 
                  mat[8] * mat[1] * mat[6] - 
                  mat[8] * mat[2] * mat[5];

        float det = mat[0] * inv[0] + mat[1] * inv[4] + mat[2] * inv[8] + mat[3] * inv[12];

        Matrix4 res;
        if (det == 0) return res;

        det = 1.0f / det;

        for (int i = 0; i < 16; i++)
            (&res.m[0][0])[i] = inv[i] * det;

        return res;
    }
};

} // namespace skwr

#endif // SKWR_CORE_MATH_MATRIX_H_
