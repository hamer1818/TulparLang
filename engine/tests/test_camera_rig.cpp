// sim/camera_rig.hpp: exponential_smooth'un "yari-omur" tanimini (dt=1
// yari-omurde TAM yariya, 2 yari-omurde TAM ceyrege iner) elle hesapla
// kanitlar -- exp() sayisal degerini bilmeye GEREK YOK, yari-omrun ikiye
// katlanma ozelligi yeterli. CameraShake icin de ayni ilke: |sin|<=1
// kimliginden max_offset siniri elle KANITLANIR, tam sinus degeri gerekmez.
#include "core/math/vec.hpp"
#include "sim/camera_rig.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

ENGINE_TEST(exponential_smooth_half_life_property) {
  // dt=0 -> HIC degisim (decay=exp(0)=1, tam olarak "current" doner).
  CHECK(exponential_smooth(3.0f, 99.0f, 1.0f, 0.0f) == 3.0f);
  // dt=1 yari-omur -> mesafe TAM YARIYA.
  CHECK(nearly_equal(exponential_smooth(0.0f, 10.0f, 1.0f, 1.0f), 5.0f, 1e-4f));
  // dt=2 yari-omur -> mesafe TAM CEYREGE (0.5^2).
  CHECK(nearly_equal(exponential_smooth(0.0f, 8.0f, 2.0f, 4.0f), 6.0f, 1e-4f));
  // dt=3 yari-omur -> mesafe TAM SEKIZDE-BIRE (0.5^3).
  CHECK(nearly_equal(exponential_smooth(0.0f, 8.0f, 1.0f, 3.0f), 7.0f, 1e-4f));
  // yari-omur<=0 -> ANINDA hedefe (ozel durum, exp hesabina girmez).
  CHECK(exponential_smooth(5.0f, 10.0f, 0.0f, 1.0f) == 10.0f);
}

ENGINE_TEST(exponential_smooth_vec3_matches_per_component) {
  Vec3 r = exponential_smooth(Vec3{0, 0, 0}, Vec3{10, -10, 4}, 1.0f, 1.0f);
  CHECK((nearly_equal(r, Vec3{5, -5, 2}, 1e-4f))); // her eksen bagimsiz yariya iner
}

ENGINE_TEST(camera_shake_add_trauma_clamps_to_unit_range) {
  CameraShake s;
  s.add_trauma(0.5f);
  CHECK(nearly_equal(s.trauma, 0.5f, 1e-6f));
  s.add_trauma(0.8f); // 0.5+0.8=1.3 -> ust sinira kirpilir
  CHECK(nearly_equal(s.trauma, 1.0f, 1e-6f));
  s.add_trauma(-5.0f); // asiri negatif -> alt sinira kirpilir
  CHECK(nearly_equal(s.trauma, 0.0f, 1e-6f));
}

ENGINE_TEST(camera_shake_zero_trauma_yields_exact_zero_offset) {
  CameraShake s; // varsayilan trauma=0
  s.decay_per_s = 0.0f;
  Vec3 off = s.update(0.016f, 1.234, 0.3f);
  CHECK((off == Vec3{0, 0, 0})); // shake=trauma^2=0 -> sin ne olursa olsun sonuc TAM 0
}

ENGINE_TEST(camera_shake_offset_never_exceeds_max_offset) {
  // |sin(x)|<=1 kimligi + shake=trauma^2<=1 (trauma [0,1]'e kirpili) ->
  // |bilesen|<=max_offset HER ZAMAN, hangi zaman/trauma degeri olursa olsun.
  CameraShake s;
  s.add_trauma(1.0f);
  s.decay_per_s = 0.0f; // trauma sabit kalsin (sinir testini bozmasin)
  for (int i = 0; i < 500; i++) {
    Vec3 off = s.update(0.01f, i * 0.0371, 0.3f);
    CHECK(off.x >= -0.3f && off.x <= 0.3f);
    CHECK(off.y >= -0.3f && off.y <= 0.3f);
    CHECK(off.z >= -0.3f && off.z <= 0.3f);
  }
}

ENGINE_TEST(camera_shake_decays_linearly_to_zero) {
  CameraShake s;
  s.add_trauma(1.0f);
  s.decay_per_s = 2.0f; // saniyede 2 birim azalir
  s.update(0.5f, 0.0);  // 0.5s * 2/s = 1.0 azalma -> trauma TAM 0
  CHECK(nearly_equal(s.trauma, 0.0f, 1e-5f));
}

ENGINE_TEST(fov_and_zoom_controllers_follow_half_life_formula) {
  // Ikisi de exponential_smooth'un dogrudan sarmalayicisi - AYNI yari-omur
  // ozelligi (zaten yukarida kanitlandi) burada sadece DOGRU alanlara
  // (fov_deg/distance) uygulandigini dogrular.
  FovController fov;
  fov.fov_deg = 60.0f;
  fov.half_life_s = 1.0f;
  float r1 = fov.update(90.0f, 1.0f); // 1 yari-omur: 60->90 mesafesinin yarisi
  CHECK(nearly_equal(r1, 75.0f, 1e-4f));
  CHECK(nearly_equal(fov.fov_deg, 75.0f, 1e-4f)); // uye de guncellendi

  ZoomController zoom;
  zoom.distance = 5.0f;
  zoom.half_life_s = 1.0f;
  float r2 = zoom.update(1.0f, 1.0f); // 5->1, 1 yari-omur: yariya (3)
  CHECK(nearly_equal(r2, 3.0f, 1e-4f));
}

ENGINE_TEST(follow_camera_position_follows_half_life_formula) {
  FollowCamera rig;
  rig.position = Vec3{0, 0, 0};
  rig.position_half_life_s = 1.0f;
  Mat4 view = rig.update(Vec3{0, 0, 0}, Vec3{0, 2, -5}, Vec3{0, 1, 0}, 1.0f); // dt = 1 yari-omur
  // Istenen konum (0,2,-5); 1 yari-omur sonra baslangica (0,0,0) olan mesafenin
  // TAM YARISI kadar yaklasir: (0,1,-2.5).
  CHECK((nearly_equal(rig.position, Vec3{0, 1, -2.5f}, 1e-4f)));
  (void)view; // Mat4::look_at'in kendisi tests/test_math.cpp'de zaten dogrulandi
}
