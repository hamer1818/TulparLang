// L1 CORE — vektor/matris/quaternion. float, sutun-major mat4, sag el.
// Simulasyon kodu STRICT FP ile derlenir (plan C.1.5: fast-math yalniz
// render/VFX); bu baslikta reassociation'a dayanan hicbir sey yok.
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

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
inline Vec4 operator-(Vec4 a, Vec4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
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

// Genel 4x4 tersi (Gauss-Jordan, kismi pivot). Nadir cagrilir (goruntu/gölge
// matrisi, editor raycast); sik cagrilan yol icin inverse_affine() kullan.
// Tekil (determinant ~0) girdi: sessizce identity doner (dosyanin geri kalanindaki
// "sifir vektor -> sifir" konvansiyonuyla ayni — cagiran assert etmez).
inline Mat4 inverse(const Mat4 &a) {
  float aug[4][8];
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) aug[r][c] = a.m[c][r];
    for (int c = 0; c < 4; c++) aug[r][4 + c] = (r == c) ? 1.0f : 0.0f;
  }
  for (int col = 0; col < 4; col++) {
    int piv = col;
    float best = std::fabs(aug[col][col]);
    for (int r = col + 1; r < 4; r++) {
      float v = std::fabs(aug[r][col]);
      if (v > best) { best = v; piv = r; }
    }
    if (best < 1e-8f) return Mat4::identity();
    if (piv != col)
      for (int c = 0; c < 8; c++) { float t = aug[col][c]; aug[col][c] = aug[piv][c]; aug[piv][c] = t; }
    float inv_p = 1.0f / aug[col][col];
    for (int c = 0; c < 8; c++) aug[col][c] *= inv_p;
    for (int r = 0; r < 4; r++) {
      if (r == col) continue;
      float f = aug[r][col];
      if (f == 0.0f) continue;
      for (int c = 0; c < 8; c++) aug[r][c] -= f * aug[col][c];
    }
  }
  Mat4 out;
  for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) out.m[c][r] = aug[r][4 + c];
  return out;
}

// TRS (oteleme*donus*olcek) matrisleri icin hizli ters — genel inverse()'den ucuz.
// ⚠ Shear (egiklik) VARSA yanlis sonuc verir, sessizce; bu baslik bagimliliksiz
// kalsin diye assert yok — cagiran yalniz build/import'tan gelen TRS matrisleriyle
// cagirmakla sorumlu (plan Is 1'in notu).
inline Mat4 inverse_affine(const Mat4 &a) {
  Vec3 c0{a.m[0][0], a.m[0][1], a.m[0][2]};
  Vec3 c1{a.m[1][0], a.m[1][1], a.m[1][2]};
  Vec3 c2{a.m[2][0], a.m[2][1], a.m[2][2]};
  Vec3 t{a.m[3][0], a.m[3][1], a.m[3][2]};
  float s0 = length(c0), s1 = length(c1), s2 = length(c2);
  float inv0 = s0 > 0 ? 1.0f / (s0 * s0) : 0.0f;
  float inv1 = s1 > 0 ? 1.0f / (s1 * s1) : 0.0f;
  float inv2 = s2 > 0 ? 1.0f / (s2 * s2) : 0.0f;
  Mat4 out;
  // Ust 3x3: S^-1 * R^T. Satir P (asagida) = c_P / s_P^2 — bkz plan turetimi.
  out.m[0][0] = c0.x * inv0; out.m[1][0] = c0.y * inv0; out.m[2][0] = c0.z * inv0;
  out.m[0][1] = c1.x * inv1; out.m[1][1] = c1.y * inv1; out.m[2][1] = c1.z * inv1;
  out.m[0][2] = c2.x * inv2; out.m[1][2] = c2.y * inv2; out.m[2][2] = c2.z * inv2;
  Vec3 ut{out.m[0][0] * t.x + out.m[1][0] * t.y + out.m[2][0] * t.z,
          out.m[0][1] * t.x + out.m[1][1] * t.y + out.m[2][1] * t.z,
          out.m[0][2] * t.x + out.m[1][2] * t.y + out.m[2][2] * t.z};
  out.m[3][0] = -ut.x; out.m[3][1] = -ut.y; out.m[3][2] = -ut.z;
  return out;
}

// Eksen hizali kutu. Varsayilan: bos kutu (min=+sonsuz, max=-sonsuz) — merge'in
// birim elemani (bos ile merge, digerini degistirmeden dondurur).
struct Aabb {
  Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
};
inline Aabb merge(Aabb a, Aabb b) { return {vmin(a.min, b.min), vmax(a.max, b.max)}; }
inline Aabb merge(Aabb a, Vec3 p) { return {vmin(a.min, p), vmax(a.max, p)}; }
inline Vec3 center(Aabb a) { return (a.min + a.max) * 0.5f; }
inline Vec3 extent(Aabb a) { return (a.max - a.min) * 0.5f; } // yari-boyut
// Kutunun yuzey alani (bos kutu icin ANLAMSIZ -- en az 1 oge birlestirilmis
// olmasi varsayilir). core/math/bvh.hpp'nin SAH insa maliyetinde kullanilir
// (NanoRT'un (MIT, lighttransport/nanort) da kullandigi klasik formul).
inline float surface_area(Aabb a) {
  const Vec3 d = a.max - a.min;
  return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
}
// Kutuyu matrisle donustur (Arvo yontemi: merkez+yari-boyut, 8 kose gerekmez).
inline Aabb transform(const Mat4 &m, Aabb a) {
  Vec3 c = transform_point(m, center(a));
  Vec3 e = extent(a);
  Vec3 ex{std::fabs(m.m[0][0]) * e.x, std::fabs(m.m[0][1]) * e.x, std::fabs(m.m[0][2]) * e.x};
  Vec3 ey{std::fabs(m.m[1][0]) * e.y, std::fabs(m.m[1][1]) * e.y, std::fabs(m.m[1][2]) * e.y};
  Vec3 ez{std::fabs(m.m[2][0]) * e.z, std::fabs(m.m[2][1]) * e.z, std::fabs(m.m[2][2]) * e.z};
  Vec3 ne = ex + ey + ez;
  return {c - ne, c + ne};
}

struct Plane {
  Vec3 normal{0, 0, 1};
  float d = 0; // duzlem: dot(normal,p) + d == 0
};
inline Plane plane_from_point_normal(Vec3 point, Vec3 normal) {
  Vec3 n = normalize(normal);
  return {n, -dot(n, point)};
}
inline float distance(Plane p, Vec3 point) { return dot(p.normal, point) + p.d; }
// (A,B,C,D) katsayilarindan normalize edilmis duzlem. Normalize etmezsen mesafe
// testi olceksiz cikar (plan Is 1 tuzagi).
inline Plane plane_from_row(Vec4 r) {
  Vec3 n{r.x, r.y, r.z};
  float len = length(n);
  float inv = len > 0 ? 1.0f / len : 0.0f;
  return {n * inv, r.w * inv};
}

struct Ray {
  Vec3 origin{0, 0, 0};
  Vec3 dir{0, 0, 1};
};
// Kayma (slab) yontemi. t_out (varsa) en yakin giris mesafesi (t<0 ise ray kutunun icinde baslar, 0 donuyoruz).
inline bool intersect(Ray ray, Aabb box, float *t_out = nullptr) {
  float tmin = 0.0f, tmax = 3.0e38f;
  const float ro[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
  const float rd[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
  const float lo[3] = {box.min.x, box.min.y, box.min.z};
  const float hi[3] = {box.max.x, box.max.y, box.max.z};
  for (int i = 0; i < 3; i++) {
    if (std::fabs(rd[i]) < 1e-12f) {
      if (ro[i] < lo[i] || ro[i] > hi[i]) return false;
      continue;
    }
    float inv = 1.0f / rd[i];
    float t1 = (lo[i] - ro[i]) * inv, t2 = (hi[i] - ro[i]) * inv;
    if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
    if (t1 > tmin) tmin = t1;
    if (t2 < tmax) tmax = t2;
    if (tmin > tmax) return false;
  }
  if (t_out) *t_out = tmin;
  return true;
}
inline bool intersect(Ray ray, Plane p, float *t_out = nullptr) {
  float denom = dot(p.normal, ray.dir);
  if (std::fabs(denom) < 1e-8f) return false; // paralel
  float t = -(dot(p.normal, ray.origin) + p.d) / denom;
  if (t < 0) return false;
  if (t_out) *t_out = t;
  return true;
}

// 2B isaretli mesafe alanlari (SDF) — UI/vektor sekil render'i icin standart,
// yaygin bilinen formuller (500 madde listesi #162 "SDF UI"; Inigo Quilez'in
// "2D distance functions" makalesindeki kanitlanmis formuller — kendi
// turetim degil, ama asagida (tests/test_math.cpp) elle nokta nokta
// dogrulandi). Donus: NEGATIF = sekil ICINDE, POZITIF = DISINDA, 0 = sinirinda.
inline float sdf_circle(Vec2 p, Vec2 center, float radius) { return length(p - center) - radius; }

// half_extents: kutunun YARI-boyutlari (genislik/2, yukseklik/2). p, kutunun
// MERKEZINE GORE verilir (cagiran donus/oteleme uygulamak icin p'yi kendi
// tasir) — bu yuzden ayri bir "center" parametresi ALMAZ.
inline float sdf_box(Vec2 p, Vec2 half_extents) {
  Vec2 d{std::fabs(p.x) - half_extents.x, std::fabs(p.y) - half_extents.y};
  Vec2 d_pos{d.x > 0.0f ? d.x : 0.0f, d.y > 0.0f ? d.y : 0.0f};
  float outside = length(d_pos);
  float inside = d.x > d.y ? d.x : d.y; // max(d.x,d.y)
  inside = inside < 0.0f ? inside : 0.0f; // min(inside,0) — yalniz GERCEKTEN icerideyken katki verir
  return outside + inside;
}

// Kutuyu `corner_radius` kadar kuculttup SDF'sini alir, sonra geri
// corner_radius kadar "sisirir" (klasik yuvarlatilmis-kutu hilesi).
inline float sdf_rounded_box(Vec2 p, Vec2 half_extents, float corner_radius) {
  return sdf_box(p, {half_extents.x - corner_radius, half_extents.y - corner_radius}) - corner_radius;
}

struct Sphere {
  Vec3 center{0, 0, 0};
  float radius = 0.0f;
};
// Klasik ikinci dereceden kok (L=O-C; |L+tD|^2=r^2 -> a*t^2+b*t+c=0). Aabb
// ile AYNI sozlesme: ray kurenin ICINDE basliyorsa (c<0 -> kokler zit isaretli,
// t0<0<t1) t=0 donuyoruz (bkz. yukaridaki Aabb intersect'in "tmin=0 baslar"
// yorumu) -- "iceride baslama" ayri bir durum degil, en yakin GECERLI isabet.
// dir'in normalize edilmis olmasi GEREKMEZ (Aabb/Plane ile ayni gelenek); t
// o zaman `dir` biriminde parametrik olur, dunya mesafesi degil.
inline bool intersect(Ray ray, Sphere s, float *t_out = nullptr) {
  Vec3 l = ray.origin - s.center;
  float a = dot(ray.dir, ray.dir);
  if (a < 1e-12f) return false; // sifir uzunlukta yon
  float b = 2.0f * dot(l, ray.dir);
  float c = dot(l, l) - s.radius * s.radius;
  float disc = b * b - 4.0f * a * c;
  if (disc < 0.0f) return false; // kure hic kesilmiyor
  float sq = std::sqrt(disc);
  float t0 = (-b - sq) / (2.0f * a);
  float t1 = (-b + sq) / (2.0f * a);
  if (t1 < 0.0f) return false; // kure tamamen ray'in GERISINDE
  if (t_out) *t_out = t0 < 0.0f ? 0.0f : t0;
  return true;
}

enum class FrustumTest { kOutside, kInside, kIntersecting };
// 6 duzlem: sol, sag, alt, ust, yakin, uzak. Dunya uzayinda, viewproj'dan
// cikarilir (Gribb/Hartmann). Vulkan NDC z in [0,1] varsayimi (bu dosyanin
// perspective/ortho'suyla ayni gelenek) — OpenGL'in [-1,1] z'siyle KARISTIRMA.
struct Frustum {
  Plane planes[6];
  static Frustum from_viewproj(const Mat4 &vp) {
    Vec4 r0{vp.m[0][0], vp.m[1][0], vp.m[2][0], vp.m[3][0]};
    Vec4 r1{vp.m[0][1], vp.m[1][1], vp.m[2][1], vp.m[3][1]};
    Vec4 r2{vp.m[0][2], vp.m[1][2], vp.m[2][2], vp.m[3][2]};
    Vec4 r3{vp.m[0][3], vp.m[1][3], vp.m[2][3], vp.m[3][3]};
    Frustum f;
    f.planes[0] = plane_from_row(r3 + r0); // sol
    f.planes[1] = plane_from_row(r3 - r0); // sag
    f.planes[2] = plane_from_row(r3 + r1); // alt
    f.planes[3] = plane_from_row(r3 - r1); // ust
    f.planes[4] = plane_from_row(r2);      // yakin: z_ndc >= 0
    f.planes[5] = plane_from_row(r3 - r2); // uzak: z_ndc <= w
    return f;
  }
};
// Pozitif/negatif kose yontemi (Ericson, RTCD): kutunun duzlem yonundeki en uzak
// noktasi negatifse tamamen disarida; en yakin noktasi da pozitifse tamamen icerde.
// ADI `test` DEGIL `frustum_test`: `tulpar::engine::test` ZATEN bir AD ALANI
// (tests/test.hpp'nin cercevesi orada yasiyor). Serbest fonksiyon olarak
// `test` koymak o ad alanini golgeliyor ve testleri iceren HER cevirim birimi
// "redeclared as different kind of entity" ile dusuyordu — tek satir, 2500+
// hata. (PR #322 derlenmedigi icin gorulememis.)
inline FrustumTest frustum_test(const Frustum &f, Aabb box) {
  Vec3 c = center(box), e = extent(box);
  bool any_intersecting = false;
  for (int i = 0; i < 6; i++) {
    const Plane &p = f.planes[i];
    float r = std::fabs(p.normal.x) * e.x + std::fabs(p.normal.y) * e.y + std::fabs(p.normal.z) * e.z;
    float d = distance(p, c);
    if (d + r < 0) return FrustumTest::kOutside;
    if (d - r < 0) any_intersecting = true;
  }
  return any_intersecting ? FrustumTest::kIntersecting : FrustumTest::kInside;
}
inline bool intersects(const Frustum &f, Aabb box) { return frustum_test(f, box) != FrustumTest::kOutside; }
// Kure-frustum testi: Sascha Willems'in (MIT, github.com/SaschaWillems/Vulkan,
// examples/computecullandlod/cull.comp) frustumCheck() ile AYNI mantik --
// her duzlem icin distance(plane,center)+radius<0 ise TAMAMEN disarida
// (yukaridaki Aabb testindeki "pozitif/negatif kose" fikrinin kure karsiligi).
inline bool intersects(const Frustum &f, Sphere s) {
  for (int i = 0; i < 6; i++) {
    if (distance(f.planes[i], s.center) + s.radius < 0.0f) return false;
  }
  return true;
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

// to_mat4(Quat)'in tersi (Shepperd yontemi, sayisal kararli): donus 3x3'ten
// quaternion. c0/c1/c2 NORMALIZE EDILMIS sutunlar olmali (olcek onceden cikarilmis).
inline Quat mat3_to_quat(Vec3 c0, Vec3 c1, Vec3 c2) {
  const float m00 = c0.x, m10 = c0.y, m20 = c0.z;
  const float m01 = c1.x, m11 = c1.y, m21 = c1.z;
  const float m02 = c2.x, m12 = c2.y, m22 = c2.z;
  const float trace = m00 + m11 + m22;
  Quat q;
  if (trace > 0.0f) {
    float s = std::sqrt(trace + 1.0f) * 2.0f;
    q.w = 0.25f * s;
    q.x = (m21 - m12) / s;
    q.y = (m02 - m20) / s;
    q.z = (m10 - m01) / s;
  } else if (m00 > m11 && m00 > m22) {
    float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    q.w = (m21 - m12) / s;
    q.x = 0.25f * s;
    q.y = (m01 + m10) / s;
    q.z = (m02 + m20) / s;
  } else if (m11 > m22) {
    float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    q.w = (m02 - m20) / s;
    q.x = (m01 + m10) / s;
    q.y = 0.25f * s;
    q.z = (m12 + m21) / s;
  } else {
    float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    q.w = (m10 - m01) / s;
    q.x = (m02 + m20) / s;
    q.y = (m12 + m21) / s;
    q.z = 0.25f * s;
  }
  return q;
}

// Oteleme/donus/olcek ayristirmasi (editor gizmo, animasyon blend). TRS
// varsayimi — shear varsa donus kismi bozulur (inverse_affine ile ayni sinir).
struct Transform {
  Vec3 translation{0, 0, 0};
  Quat rotation = Quat::identity();
  Vec3 scale{1, 1, 1};
};
inline Transform decompose(const Mat4 &m) {
  Transform t;
  t.translation = {m.m[3][0], m.m[3][1], m.m[3][2]};
  Vec3 c0{m.m[0][0], m.m[0][1], m.m[0][2]};
  Vec3 c1{m.m[1][0], m.m[1][1], m.m[1][2]};
  Vec3 c2{m.m[2][0], m.m[2][1], m.m[2][2]};
  float sx = length(c0), sy = length(c1), sz = length(c2);
  // Negatif determinant = ayna; bir eksenin isaretini geri vererek donus kismini
  // gercek bir donus (ayna degil) olarak tut.
  if (dot(cross(c0, c1), c2) < 0.0f) sx = -sx;
  t.scale = {sx, sy, sz};
  Vec3 r0 = sx != 0 ? c0 * (1.0f / sx) : Vec3{1, 0, 0};
  Vec3 r1 = sy != 0 ? c1 * (1.0f / sy) : Vec3{0, 1, 0};
  Vec3 r2 = sz != 0 ? c2 * (1.0f / sz) : Vec3{0, 0, 1};
  t.rotation = mat3_to_quat(r0, r1, r2);
  return t;
}

} // namespace tulpar::engine
