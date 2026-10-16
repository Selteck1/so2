#pragma once

#include <cmath>
#include <cstdint>

namespace xim {

struct vec2 {
    float x{}, y{};
    vec2() = default;
    vec2(float x_, float y_) : x(x_), y(y_) {}

    vec2 operator+(const vec2& o) const { return {x + o.x, y + o.y}; }
    vec2 operator-(const vec2& o) const { return {x - o.x, y - o.y}; }
};

struct vec3 {
    float x{}, y{}, z{};
    vec3() = default;
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator*(float s)      const { return {x * s, y * s, z * s}; }

    float dot(const vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length_sq()        const { return x * x + y * y + z * z; }
    float length()           const { return std::sqrt(length_sq()); }
};

struct vec4 {
    float x{}, y{}, z{}, w{};
};

// column-major 4x4, как в Unity/OpenGL
struct mat4 {
    float m[16]{};

    vec4 mul_vec4(const vec4& v) const {
        return {
            m[0]*v.x + m[4]*v.y + m[8] *v.z + m[12]*v.w,
            m[1]*v.x + m[5]*v.y + m[9] *v.z + m[13]*v.w,
            m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]*v.w,
            m[3]*v.x + m[7]*v.y + m[11]*v.z + m[15]*v.w
        };
    }
};

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace xim