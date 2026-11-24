// Leptid — math primitives: Vec2, affine transforms, easing, angles.
#pragma once
#include <cmath>
#include <algorithm>

namespace leptid {

struct Vec2 {
    float x = 0, y = 0;
    Vec2() {}
    Vec2(float X, float Y) : x(X), y(Y) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2 operator-() const { return {-x, -y}; }
    bool operator==(const Vec2& o) const { return std::fabs(x - o.x) < 1e-5f && std::fabs(y - o.y) < 1e-5f; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }
    float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    float cross(const Vec2& o) const { return x * o.y - y * o.x; }
    float len() const { return std::sqrt(x * x + y * y); }
    float len2() const { return x * x + y * y; }
    Vec2 normalized() const {
        float l = len();
        return l > 1e-9f ? Vec2(x / l, y / l) : Vec2(1, 0);
    }
    static float dist(const Vec2& a, const Vec2& b) { return (a - b).len(); }
    // Signed angle of vector (radians, atan2 frame, y-down screen space friendly).
    static float angleOf(const Vec2& v) { return std::atan2(v.y, v.x); }
    static Vec2 fromAngle(float a) { return Vec2(std::cos(a), std::sin(a)); }
    static float angleBetween(const Vec2& a, const Vec2& b) {
        float d = a.dot(b) / (a.len() * b.len() + 1e-12f);
        return std::acos(std::max(-1.0f, std::min(1.0f, d)));
    }
};
inline Vec2 operator*(float s, const Vec2& v) { return Vec2(v.x * s, v.y * s); }

// 2D affine transform: [a c e; b d f] (x' = a*x + c*y + e, y' = b*x + d*y + f).
struct Mat3 {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; // b,c swapped vs. textbook: c = shear x by y
    static Mat3 identity() { return Mat3(); }
    static Mat3 translation(float tx, float ty) { Mat3 m; m.e = tx; m.f = ty; return m; }
    static Mat3 scale(float s) { Mat3 m; m.a = m.d = s; return m; }
    static Mat3 scale(float sx, float sy) { Mat3 m; m.a = sx; m.d = sy; return m; }
    static Mat3 rotation(float rad) {
        Mat3 m; float c = std::cos(rad), s = std::sin(rad);
        m.a = c; m.b = s; m.c = -s; m.d = c; return m;
    }
    Mat3 operator*(const Mat3& o) const {
        Mat3 r;
        r.a = a * o.a + c * o.b; r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d; r.d = b * o.c + d * o.d;
        r.e = a * o.e + c * o.f + e; r.f = b * o.e + d * o.f + f;
        return r;
    }
    Vec2 apply(float x, float y) const { return Vec2(a * x + c * y + e, b * x + d * y + f); }
    Vec2 apply(const Vec2& v) const { return apply(v.x, v.y); }
    Mat3 inverse() const {
        float det = a * d - b * c;
        if (std::fabs(det) < 1e-12f) det = det >= 0 ? 1e-12f : -1e-12f;
        float ia = d / det, ib = -b / det, ic = -c / det, id = a / det;
        Mat3 r;
        r.a = ia; r.b = ib; r.c = ic; r.d = id;
        r.e = -(ia * e + ic * f); r.f = -(ib * e + id * f);
        return r;
    }
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }

// Easing functions for keyframe tweening (t in [0,1]).
inline float easeLinear(float t) { return t; }
inline float easeInCubic(float t) { return t * t * t; }
inline float easeOutCubic(float t) { float u = 1 - t; return 1 - u * u * u; }
inline float easeInOutCubic(float t) {
    t = clampf(t, 0, 1);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}
inline float easeInOutSine(float t) { return -(std::cos(3.14159265358979f * t) - 1) / 2; }

inline float degToRad(float d) { return d * 3.14159265358979f / 180.0f; }
inline float radToDeg(float r) { return r * 180.0f / 3.14159265358979f; }

// Wrap angle into (-PI, PI].
inline float normalizeAngle(float a) {
    while (a > 3.14159265358979f) a -= 2 * 3.14159265358979f;
    while (a <= -3.14159265358979f) a += 2 * 3.14159265358979f;
    return a;
}
// Shortest-path angle interpolation.
inline float lerpAngle(float a, float b, float t) {
    float d = normalizeAngle(b - a);
    return a + d * t;
}
// Clamp angle `a` into the allowed arc [minA, maxA] (absolute angles; the arc may
// wrap through ±PI). Out-of-range values snap to the NEAREST boundary in angular
// distance — important for IK joint limits, where a 200° elbow must bend to the
// closest legal angle, not teleport to the other end of the arc.
inline float clampAngleRange(float a, float minA, float maxA) {
    a = normalizeAngle(a);
    minA = normalizeAngle(minA);
    maxA = normalizeAngle(maxA);
    if (minA <= maxA) {
        if (a >= minA && a <= maxA) return a;
        float dMin = std::fabs(normalizeAngle(minA - a));
        float dMax = std::fabs(normalizeAngle(a - maxA));
        return dMin <= dMax ? minA : maxA;
    }
    // Range wraps through ±PI (e.g., +150° .. -150° means "everything except the seam").
    if (a >= minA || a <= maxA) return a;
    float dMin = std::fabs(normalizeAngle(a - minA));
    float dMax = std::fabs(normalizeAngle(a - maxA));
    return dMin < dMax ? minA : maxA;
}

} // namespace leptid
