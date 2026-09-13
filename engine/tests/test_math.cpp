#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(math_vec3_basics) {
  Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
  CHECK(cross(x, y) == z);
  CHECK(cross(y, x) == -z);
  CHECK(dot(x, y) == 0.0f);
  CHECK(nearly_equal(length(Vec3{3, 4, 0}), 5.0f));
  CHECK(nearly_equal(length(normalize(Vec3{3, 4, 12})), 1.0f));
  CHECK(nearly_equal(lerp(x, y, 0.5f), Vec3{0.5f, 0.5f, 0}));
  CHECK(normalize(Vec3{}) == Vec3{}); // sifir vektor: NaN degil
}

ENGINE_TEST(math_mat4_transforms) {
  Mat4 t = Mat4::translate({1, 2, 3});
  CHECK(nearly_equal(transform_point(t, {0, 0, 0}), {1, 2, 3}));
  CHECK(nearly_equal(transform_dir(t, {1, 0, 0}), {1, 0, 0})); // yon cevriden etkilenmez
  Mat4 r = Mat4::rotate({0, 0, 1}, kPi * 0.5f);
  CHECK(nearly_equal(transform_point(r, {1, 0, 0}), {0, 1, 0}));
  Mat4 s = Mat4::scale({2, 2, 2});
  Mat4 trs = t * r * s; // once olcek, sonra donus, sonra ceviri
  CHECK(nearly_equal(transform_point(trs, {1, 0, 0}), {1, 4, 3}));
  Mat4 i = Mat4::identity() * Mat4::identity();
  CHECK(i.m[0][0] == 1 && i.m[3][3] == 1 && i.m[1][0] == 0);
}

ENGINE_TEST(math_perspective_and_look_at) {
  Mat4 p = Mat4::perspective(kPi * 0.5f, 1.0f, 0.1f, 100.0f);
  Vec4 n = p * Vec4{0, 0, -0.1f, 1};
  Vec4 f = p * Vec4{0, 0, -100.0f, 1};
  CHECK(nearly_equal(n.z / n.w, 0.0f));      // Vulkan: yakin duzlem z=0
  CHECK(nearly_equal(f.z / f.w, 1.0f, 1e-4f)); // uzak duzlem z=1
  Mat4 v = Mat4::look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
  CHECK(nearly_equal(transform_point(v, {0, 0, 0}), {0, 0, -5})); // hedef -z'de
  CHECK(nearly_equal(transform_point(v, {0, 0, 5}), {0, 0, 0}));  // goz orijinde
}

ENGINE_TEST(math_quat) {
  Quat q = Quat::axis_angle({0, 0, 1}, kPi * 0.5f);
  CHECK(nearly_equal(rotate(q, {1, 0, 0}), {0, 1, 0}));
  CHECK(nearly_equal(transform_point(to_mat4(q), {1, 0, 0}), {0, 1, 0}));
  Quat qq = q * q; // 180 derece
  CHECK(nearly_equal(rotate(qq, {1, 0, 0}), {-1, 0, 0}));
  CHECK(nearly_equal(rotate(q * conjugate(q), {1, 2, 3}), {1, 2, 3}));
  // slerp: 45 derece = 90'in yarisi. ⚠ 180 dereceyle (qq) SINANMAZ: 180'in
  // iki esit-kisa yolu var ve hangisinin secilecegine dot(a,b)'nin ISARETI
  // karar verir; dot = c*c - s*s ve sin(pi/4) ile cos(pi/4) float'ta libm'e
  // gore son ulp'ta farkli olabiliyor. Olculdu 2026-09-14: x86_64 glibc'de
  // dot = 0 (yol A), macOS arm64'te dot < 0 (yol B) ve test dustu. Hata
  // slerp'te degil, belirsiz girdiyi sinayan testte.
  Quat h = slerp(Quat::identity(), q, 0.5f);
  CHECK(nearly_equal(rotate(h, {1, 0, 0}), {0.70710678f, 0.70710678f, 0}, 1e-4f));
  Quat h1 = slerp(Quat::identity(), q, 1.0f);
  CHECK(nearly_equal(rotate(h1, {1, 0, 0}), {0, 1, 0}, 1e-4f));
  CHECK(nearly_equal(length(normalize(Quat{1, 2, 3, 4})), 1.0f));
}
