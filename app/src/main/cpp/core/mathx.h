#pragma once
#include <math.h>
#include <stdint.h>

static const float PI = 3.14159265358979f;

struct V2 { float x, y; };
struct V3 {
    float x, y, z;
    V3() : x(0), y(0), z(0) {}
    V3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct V4 { float x, y, z, w; };

inline V3 operator+(V3 a, V3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 operator-(V3 a, V3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 operator-(V3 a) { return V3(-a.x, -a.y, -a.z); }
inline V3 operator*(V3 a, float s) { return V3(a.x * s, a.y * s, a.z * s); }
inline V3 operator*(float s, V3 a) { return V3(a.x * s, a.y * s, a.z * s); }
inline V3 mul(V3 a, V3 b) { return V3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline V3& operator+=(V3& a, V3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline V3& operator-=(V3& a, V3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float len(V3 a) { return sqrtf(dot(a, a)); }
inline V3 norm(V3 a) { float l = len(a); return l > 1e-8f ? a * (1.0f / l) : V3(0, 1, 0); }
inline V3 lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float sat(float v) { return clampf(v, 0.f, 1.f); }
inline float smooth(float e0, float e1, float x) { float t = sat((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
inline float signf(float v) { return v < 0 ? -1.f : 1.f; }
inline float wrapAngle(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
inline float approach(float cur, float target, float rate, float dt) { return target + (cur - target) * expf(-rate * dt); }

// Column-major 4x4 matrix (OpenGL convention).
struct M4 {
    float m[16];
    static M4 ident() { M4 r; for (int i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1.f : 0.f; return r; }
};
inline M4 operator*(const M4& a, const M4& b) {
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
inline M4 perspective(float fovy, float aspect, float n, float f) {
    M4 r; for (int i = 0; i < 16; i++) r.m[i] = 0;
    float t = 1.f / tanf(fovy * 0.5f);
    r.m[0] = t / aspect; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1; r.m[14] = 2 * f * n / (n - f);
    return r;
}
inline M4 lookAt(V3 eye, V3 at, V3 up) {
    V3 f = norm(at - eye), s = norm(cross(f, up)), u = cross(s, f);
    M4 r = M4::ident();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
    return r;
}
inline M4 translate(V3 t) { M4 r = M4::ident(); r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
inline M4 scaleM(V3 s) { M4 r = M4::ident(); r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
inline M4 rotY(float a) { M4 r = M4::ident(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
inline M4 rotX(float a) { M4 r = M4::ident(); float c = cosf(a), s = sinf(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
inline M4 rotZ(float a) { M4 r = M4::ident(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }
inline M4 ortho(float l, float r_, float b, float t, float n, float f) {
    M4 r = M4::ident();
    r.m[0] = 2 / (r_ - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
    return r;
}
inline V3 xformPoint(const M4& m, V3 p) {
    return V3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
              m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}

// Deterministic RNG + value noise.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (next() & 0xFFFFFF) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * f(); }
};
inline float hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}
inline float vnoise(float x, float y) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
inline float fbm(float x, float y, int oct = 4) {
    float s = 0, a = 0.5f, tot = 0;
    for (int i = 0; i < oct; i++) { s += vnoise(x, y) * a; tot += a; x *= 2.03f; y *= 2.03f; a *= 0.5f; }
    return s / tot;
}
