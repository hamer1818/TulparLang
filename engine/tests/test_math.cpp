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

// Golge haritasi ortografik projeksiyonu: Vulkan gelenegi (z in [0,1], y asagi).
// Yanlis z esleme = golge haritasi ya hep 0 ya hep 1 olur ve "golge yok" gibi
// gorunur; bu yuzden sinirlar sayisal olarak sinanir.
ENGINE_TEST(math_ortho_matches_vulkan_depth_range) {
  const float n = 1.0f, f = 21.0f;
  Mat4 o = Mat4::ortho(-10, 10, -6, 6, n, f);
  // Yakin duzlem -> z_ndc 0, uzak duzlem -> z_ndc 1 (goruntuleme uzayinda -z ileri).
  Vec4 pn = o * Vec4{0, 0, -n, 1};
  Vec4 pf = o * Vec4{0, 0, -f, 1};
  bool near_zero = std::fabs(pn.z / pn.w - 0.0f) < 1e-5f;
  bool far_one = std::fabs(pf.z / pf.w - 1.0f) < 1e-5f;
  CHECK(near_zero);
  CHECK(far_one);
  // x sag kenar +1, y ust kenar -1 (y asagi: perspective ile ayni isaret).
  Vec4 px = o * Vec4{10, 0, -5, 1};
  Vec4 py = o * Vec4{0, 6, -5, 1};
  bool x_right = std::fabs(px.x / px.w - 1.0f) < 1e-5f;
  bool y_up_is_negative = py.y / py.w < -0.99f;
  CHECK(x_right);
  CHECK(y_up_is_negative);
  // Perspektifle ayni y isareti: dunyada yukari olan ekranda yukari olmali.
  Mat4 pp = Mat4::perspective(1.0f, 1.0f, 0.1f, 100.0f);
  Vec4 qy = pp * Vec4{0, 1, -5, 1};
  bool same_sign = (qy.y / qy.w < 0) == (py.y / py.w < 0);
  CHECK(same_sign);
}

ENGINE_TEST(math_inverse_roundtrip) {
  Mat4 t = Mat4::translate({1, 2, 3});
  Mat4 r = Mat4::rotate({0, 0, 1}, kPi * 0.5f);
  Mat4 s = Mat4::scale({2, 2, 2});
  Mat4 trs = t * r * s;
  Mat4 inv = inverse(trs);
  Mat4 id1 = trs * inv, id2 = inv * trs;
  for (int c = 0; c < 4; c++)
    for (int rr = 0; rr < 4; rr++) {
      float expect = c == rr ? 1.0f : 0.0f;
      CHECK(nearly_equal(id1.m[c][rr], expect, 1e-4f));
      CHECK(nearly_equal(id2.m[c][rr], expect, 1e-4f));
    }
  // Afin olmayan (perspektif) matris de tersine cevrilebilmeli — inverse_affine
  // bunu YAPAMAZ (asagida ayri test), genel inverse() yapmali.
  Mat4 p = Mat4::perspective(kPi * 0.5f, 1.0f, 0.1f, 100.0f);
  Mat4 pid = p * inverse(p);
  for (int c = 0; c < 4; c++)
    for (int rr = 0; rr < 4; rr++) CHECK(nearly_equal(pid.m[c][rr], c == rr ? 1.0f : 0.0f, 1e-3f));
}

ENGINE_TEST(math_inverse_affine_matches_general) {
  Mat4 t = Mat4::translate({-3, 5, 2});
  Mat4 r = Mat4::rotate({0, 1, 0}, kPi * 0.3f);
  Mat4 s = Mat4::scale({2, 0.5f, 3});
  Mat4 trs = t * r * s;
  Mat4 fast = inverse_affine(trs);
  Mat4 general = inverse(trs);
  for (int c = 0; c < 4; c++)
    for (int rr = 0; rr < 4; rr++) CHECK(nearly_equal(fast.m[c][rr], general.m[c][rr], 1e-3f));
  Vec3 p{1, 1, 1};
  CHECK(nearly_equal(transform_point(fast, transform_point(trs, p)), p, 1e-3f));
}

ENGINE_TEST(math_aabb_basics) {
  Aabb a{{-1, -1, -1}, {1, 1, 1}};
  Aabb b{{0, 0, 0}, {3, 2, 5}};
  Aabb m = merge(a, b);
  CHECK(nearly_equal(m.min, {-1, -1, -1}));
  CHECK(nearly_equal(m.max, {3, 2, 5}));
  CHECK(nearly_equal(center(a), {0, 0, 0}));
  CHECK(nearly_equal(extent(a), {1, 1, 1}));
  // 90 derece Y etrafinda donus: asimetrik kutuda X/Z yari-boyutlari yer degistirir.
  Aabb box{{-1, -2, -3}, {1, 2, 3}}; // yari-boyut (1,2,3)
  Mat4 rot90 = Mat4::rotate({0, 1, 0}, kPi * 0.5f);
  Aabb rotated = transform(rot90, box);
  CHECK(nearly_equal(extent(rotated), {3, 2, 1}, 1e-4f));
  CHECK(nearly_equal(center(rotated), {0, 0, 0}, 1e-4f));
  Mat4 tr = Mat4::translate({5, 0, 0});
  Aabb moved = transform(tr, box);
  CHECK(nearly_equal(center(moved), {5, 0, 0}, 1e-4f));
  CHECK(nearly_equal(extent(moved), {1, 2, 3}, 1e-4f));
}

ENGINE_TEST(math_plane_and_ray) {
  Plane ground = plane_from_point_normal({0, 5, 0}, {0, 1, 0}); // y=5 duzlemi
  CHECK(nearly_equal(distance(ground, {3, 5, -2}), 0.0f, 1e-5f));
  CHECK(nearly_equal(distance(ground, {0, 8, 0}), 3.0f, 1e-5f));

  Ray down{{0, 10, 0}, {0, -1, 0}};
  float t = -1;
  CHECK(intersect(down, ground, &t));
  CHECK(nearly_equal(t, 5.0f, 1e-4f));
  CHECK(!intersect(Ray{{0, 10, 0}, {1, 0, 0}}, ground)); // duzleme paralel

  Aabb box{{-1, -1, -1}, {1, 1, 1}};
  Ray hit{{-5, 0, 0}, {1, 0, 0}};
  float th = -1;
  CHECK(intersect(hit, box, &th));
  CHECK(nearly_equal(th, 4.0f, 1e-4f)); // -5'ten kutunun yakin yuzune (-1) 4 birim
  CHECK(!intersect(Ray{{-5, 5, 0}, {1, 0, 0}}, box)); // kutunun ustunden gecer
}

ENGINE_TEST(math_sdf_2d_shapes) {
  // Cember: merkezde -yaricap, sinirinda 0, 2*yaricap uzaklikta +yaricap.
  CHECK(nearly_equal(sdf_circle({0, 0}, {0, 0}, 2.0f), -2.0f, 1e-5f));
  CHECK(nearly_equal(sdf_circle({2, 0}, {0, 0}, 2.0f), 0.0f, 1e-5f));
  CHECK(nearly_equal(sdf_circle({4, 0}, {0, 0}, 2.0f), 2.0f, 1e-5f));

  // Kutu (yari-boy 2,3): merkezde -min(2,3)=-2, sag kenarda (2,0) 0,
  // (3,0)'da +1, kose disi (3,4)'te tam dogrusal mesafe sqrt(2).
  CHECK(nearly_equal(sdf_box({0, 0}, {2, 3}), -2.0f, 1e-5f));
  CHECK(nearly_equal(sdf_box({2, 0}, {2, 3}), 0.0f, 1e-5f));
  CHECK(nearly_equal(sdf_box({3, 0}, {2, 3}), 1.0f, 1e-5f));
  CHECK(nearly_equal(sdf_box({3, 4}, {2, 3}), 1.41421356f, 1e-4f));

  // Yuvarlatilmis kutu: orijinal SIVRI kosedeki (2,3) nokta, r=0.5 ile
  // yuvarlatilmis yuzeyden TAM sqrt(0.5)-0.5 (~0.2071) uzakta olmali
  // (elle turetildi: kuculmus kutunun kosesi (1.5,2.5), (2,3)'e mesafe
  // sqrt(0.5^2+0.5^2)=sqrt(0.5), eksi yaricap 0.5).
  CHECK(nearly_equal(sdf_rounded_box({2, 3}, {2, 3}, 0.5f), std::sqrt(0.5f) - 0.5f, 1e-4f));
}

ENGINE_TEST(math_sphere_and_ray) {
  Sphere s{{0, 0, 0}, 2.0f};
  float t = -1;
  // Disaridan, merkeze dogru: -5'ten kurenin yakin yuzeyine (x=-2) 3 birim.
  CHECK(intersect(Ray{{-5, 0, 0}, {1, 0, 0}}, s, &t));
  CHECK(nearly_equal(t, 3.0f, 1e-4f));
  // Tam tegetten kacirir (y=2, yaricap 2): matematiksel olarak tegetir (disc=0),
  // ama ray x ekseninde ilerlerken kureyi (0,2,0) noktasindan SIYIRIR.
  CHECK(intersect(Ray{{-5, 2, 0}, {1, 0, 0}}, s, &t));
  CHECK(nearly_equal(t, 5.0f, 1e-3f)); // tegetten (0,2,0)'a: -5'ten 5 birim
  // Acikca ISKALAR (y=3 > yaricap): kesisim yok.
  CHECK(!intersect(Ray{{-5, 3, 0}, {1, 0, 0}}, s));
  // Ray'in KENDISI kurenin icinde basliyor: sozlesme geregi t=0.
  CHECK(intersect(Ray{{0, 0, 0}, {1, 0, 0}}, s, &t));
  CHECK(nearly_equal(t, 0.0f, 1e-5f));
  // Kure tamamen GERIDE (ray ondan uzaklasiyor): kesisim yok.
  CHECK(!intersect(Ray{{-5, 0, 0}, {-1, 0, 0}}, s));
}

ENGINE_TEST(math_frustum_culls_known_aabbs) {
  Mat4 view = Mat4::look_at({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
  Mat4 proj = Mat4::perspective(kPi * 0.5f, 1.0f, 0.1f, 100.0f);
  Frustum f = Frustum::from_viewproj(proj * view);

  Aabb inside{{-1, -1, -1}, {1, 1, 1}}; // kamera onunde, derinlik ~10
  CHECK(frustum_test(f, inside) == FrustumTest::kInside);

  Aabb behind{{50, 50, 50}, {52, 52, 52}}; // kameranin arkasinda ve yaninda
  CHECK(frustum_test(f, behind) == FrustumTest::kOutside);
  CHECK(!intersects(f, behind));

  Aabb straddle_far{{-5, -5, -95}, {5, 5, -85}}; // uzak duzlemi (derinlik 100) keser
  CHECK(frustum_test(f, straddle_far) == FrustumTest::kIntersecting);
  CHECK(intersects(f, straddle_far));

  // Pozitif kontrol: kamera ters yone bakinca, once "inside" olan artik disarida.
  Mat4 view2 = Mat4::look_at({0, 0, 10}, {0, 0, 20}, {0, 1, 0});
  Frustum f2 = Frustum::from_viewproj(proj * view2);
  CHECK(frustum_test(f2, inside) == FrustumTest::kOutside);
}

// renderer/indirect_cull.hpp'nin (Sascha Willems cull.comp portu) dayandigi
// kure-frustum testi -- ayni frustum/kamera kurulumunu math_frustum_culls_
// known_aabbs ile PAYLASIR (zaten AABB'lerle dogrulanmis).
ENGINE_TEST(math_frustum_culls_known_spheres) {
  Mat4 view = Mat4::look_at({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
  Mat4 proj = Mat4::perspective(kPi * 0.5f, 1.0f, 0.1f, 100.0f);
  Frustum f = Frustum::from_viewproj(proj * view);

  CHECK(intersects(f, Sphere{{0, 0, 0}, 1.0f}));      // kamera onunde, derinlik ~10 -- ayni "inside" bolgesi
  CHECK(!intersects(f, Sphere{{50, 50, 50}, 1.0f}));  // kucuk kure, kameranin cok disinda

  // Merkezi ACIKCA disarida ama yaricapi o kadar BUYUK ki bilinen-ICERIDE
  // noktayi (orijin) kapsiyor -- kure orijini icerdigine gore frustum'la
  // KESISMEK ZORUNDA (frustum eşitliklerine GUVENMEDEN, salt geometriden
  // cikan bir dogrulama): |((50,50,50)-(0,0,0)| = sqrt(7500) =~ 86.6 < 100.
  CHECK(intersects(f, Sphere{{50, 50, 50}, 100.0f}));
}

ENGINE_TEST(math_decompose_roundtrip) {
  Vec3 pos{2, -3, 7};
  Quat rot = Quat::axis_angle({0, 0, 1}, kPi * 0.25f);
  Vec3 scl{1.5f, 2.0f, 0.5f};
  Mat4 m = Mat4::translate(pos) * to_mat4(rot) * Mat4::scale(scl);
  Transform t = decompose(m);
  CHECK(nearly_equal(t.translation, pos, 1e-4f));
  CHECK(nearly_equal(t.scale, scl, 1e-4f));
  // q ve -q ayni donusu temsil eder; bilesen karsilastirmasi yanlis-pozitif
  // verebilir, bir vektore uygulayip karsilastir.
  Vec3 probe{1, 0, 0};
  CHECK(nearly_equal(rotate(t.rotation, probe), rotate(rot, probe), 1e-3f));
  Mat4 recomposed = Mat4::translate(t.translation) * to_mat4(t.rotation) * Mat4::scale(t.scale);
  for (int c = 0; c < 4; c++)
    for (int rr = 0; rr < 4; rr++) CHECK(nearly_equal(recomposed.m[c][rr], m.m[c][rr], 1e-3f));
}
