// L1 CORE — vektor/matris/quaternion. float, sutun-major mat4, sag el.
// Simulasyon kodu STRICT FP ile derlenir (plan C.1.5: fast-math yalniz
// render/VFX); bu baslikta reassociation'a dayanan hicbir sey yok.
#pragma once
#include <cmath>
#include <cstdint>

namespace tulpar::engine {

constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
  float x = 0, y = 0;
};
struct Vec3 {
  float x = 0, y = 0, z = 0;
};
struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 &operator+=(Vec3 &a, Vec3 b) { a = a + b; return a; }
inline Vec3 &operator-=(Vec3 &a, Vec3 b) { a = a - b; return a; }
inline bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length_sq(Vec3 a) { return dot(a, a); }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
  float l = length(a);
  return l > 0 ? a * (1.0f / l) : Vec3{};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(Vec3 a, Vec3 b) { return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z}; }
inline Vec3 vmax(Vec3 a, Vec3 b) { return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z}; }
inline bool nearly_equal(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
inline bool nearly_equal(Vec3 a, Vec3 b, float eps = 1e-5f) {
  return nearly_equal(a.x, b.x, eps) && nearly_equal(a.y, b.y, eps) && nearly_equal(a.z, b.z, eps);
}

inline Vec4 operator+(Vec4 a, Vec4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
inline Vec4 operator*(Vec4 a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
inline float dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// Sutun-major: m[sutun][satir]; m[3] = ceviri sutunu (GLSL/Vulkan uyumu).
struct Mat4 {
  float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
  static Mat4 identity() { return Mat4{}; }
  static Mat4 translate(Vec3 t) {
    Mat4 r;
    r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
    return r;
  }
  static Mat4 scale(Vec3 s) {
    Mat4 r;
    r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
    return r;
  }
  // Eksen etrafinda donus (radyan), eksen normalize olmali.
  static Mat4 rotate(Vec3 axis, float rad) {
    float c = std::cos(rad), s = std::sin(rad), t = 1 - c;
    Mat4 r;
    r.m[0][0] = t * axis.x * axis.x + c;
    r.m[0][1] = t * axis.x * axis.y + s * axis.z;
    r.m[0][2] = t * axis.x * axis.z - s * axis.y;
    r.m[1][0] = t * axis.x * axis.y - s * axis.z;
    r.m[1][1] = t * axis.y * axis.y + c;
    r.m[1][2] = t * axis.y * axis.z + s * axis.x;
    r.m[2][0] = t * axis.x * axis.z + s * axis.y;
    r.m[2][1] = t * axis.y * axis.z - s * axis.x;
    r.m[2][2] = t * axis.z * axis.z + c;
    return r;
  }
  // Vulkan: NDC z in [0,1], y asagi. fovy radyan.
  static Mat4 perspective(float fovy, float aspect, float znear, float zfar) {
    float f = 1.0f / std::tan(fovy * 0.5f);
    Mat4 r;
    r.m[0][0] = f / aspect;
    r.m[1][1] = -f;
    r.m[2][2] = zfar / (znear - zfar);
    r.m[2][3] = -1.0f;
    r.m[3][2] = (znear * zfar) / (znear - zfar);
    r.m[3][3] = 0.0f;
    return r;
  }
  // Vulkan ortografik: NDC z in [0,1], y asagi (perspective ile ayni gelenek).
  // Yonlu isik golge haritasi icin: isik uzayinda eksen hizali kutu.
  static Mat4 ortho(float l, float r, float b, float t, float znear, float zfar) {
    Mat4 o;
    o.m[0][0] = 2.0f / (r - l);
    o.m[1][1] = -2.0f / (t - b);
    o.m[2][2] = -1.0f / (zfar - znear);
    o.m[3][0] = -(r + l) / (r - l);
    o.m[3][1] = (t + b) / (t - b);
    o.m[3][2] = -znear / (zfar - znear);
    o.m[3][3] = 1.0f;
    return o;
  }
  static Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = normalize(target - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r;
    r.m[0][0] = s.x; r.m[1][0] = s.y; r.m[2][0] = s.z;
    r.m[0][1] = u.x; r.m[1][1] = u.y; r.m[2][1] = u.z;
    r.m[0][2] = -f.x; r.m[1][2] = -f.y; r.m[2][2] = -f.z;
    r.m[3][0] = -dot(s, eye);
    r.m[3][1] = -dot(u, eye);
    r.m[3][2] = dot(f, eye);
    return r;
  }
};

inline Mat4 operator*(const Mat4 &a, const Mat4 &b) {
  Mat4 r;
  for (int c = 0; c < 4; c++)
    for (int rr = 0; rr < 4; rr++) {
      float acc = 0;
      for (int k = 0; k < 4; k++) acc += a.m[k][rr] * b.m[c][k];
      r.m[c][rr] = acc;
    }
  return r;
}
inline Vec4 operator*(const Mat4 &a, Vec4 v) {
  Vec4 r;
  r.x = a.m[0][0] * v.x + a.m[1][0] * v.y + a.m[2][0] * v.z + a.m[3][0] * v.w;
  r.y = a.m[0][1] * v.x + a.m[1][1] * v.y + a.m[2][1] * v.z + a.m[3][1] * v.w;
  r.z = a.m[0][2] * v.x + a.m[1][2] * v.y + a.m[2][2] * v.z + a.m[3][2] * v.w;
  r.w = a.m[0][3] * v.x + a.m[1][3] * v.y + a.m[2][3] * v.z + a.m[3][3] * v.w;
  return r;
}
inline Vec3 transform_point(const Mat4 &a, Vec3 p) {
  Vec4 r = a * Vec4{p.x, p.y, p.z, 1.0f};
  return {r.x, r.y, r.z};
}
inline Vec3 transform_dir(const Mat4 &a, Vec3 d) {
  Vec4 r = a * Vec4{d.x, d.y, d.z, 0.0f};
  return {r.x, r.y, r.z};
}

struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
  static Quat identity() { return Quat{}; }
  static Quat axis_angle(Vec3 axis, float rad) {
    float s = std::sin(rad * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(rad * 0.5f)};
  }
};
inline Quat operator*(Quat a, Quat b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline float length(Quat q) { return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); }
inline Quat normalize(Quat q) {
  float l = length(q);
  return l > 0 ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{};
}
inline Vec3 rotate(Quat q, Vec3 v) {
  Vec3 u{q.x, q.y, q.z};
  return u * (2.0f * dot(u, v)) + v * (q.w * q.w - dot(u, u)) + cross(u, v) * (2.0f * q.w);
}
inline Mat4 to_mat4(Quat q) {
  float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  Mat4 r;
  r.m[0][0] = 1 - 2 * (yy + zz); r.m[0][1] = 2 * (xy + wz);     r.m[0][2] = 2 * (xz - wy);
  r.m[1][0] = 2 * (xy - wz);     r.m[1][1] = 1 - 2 * (xx + zz); r.m[1][2] = 2 * (yz + wx);
  r.m[2][0] = 2 * (xz + wy);     r.m[2][1] = 2 * (yz - wx);     r.m[2][2] = 1 - 2 * (xx + yy);
  return r;
}
// Normalize quaternion'lar arasi kuresel interpolasyon.
inline Quat slerp(Quat a, Quat b, float t) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
  if (d > 0.9995f) return normalize(Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
  float th = std::acos(d), s = std::sin(th);
  float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
  return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

} // namespace tulpar::engine
