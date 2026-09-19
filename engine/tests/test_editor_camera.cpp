// editor_camera kapilari: gezinme SAF bir fonksiyon oldugu icin hepsi
// Vulkan'siz, analitik olculur — pencere acmaya, GPU'ya, ImGui'ye gerek yok.
//
// Her kapinin OLUMLU ve OLUMSUZ kontrolu var (kontrolu olmayan olcum, olcum
// degildir):
//  1) Yorunge donusu + pitch kenedi IKI UCTA; kontrol: arayuz fareyi yuttugunda
//     (allow_mouse = false) hicbir sey degismez.
//  2) Kaydirma bakisa DIK; kontrol: ayni surukleme bakis yonunde HIC yol almaz
//     ve sag tus (shift'siz) hedefi OYNATMAZ (o donustur).
//  3) Yakinlik CARPIMSAL (iki adim = oranin karesi) ve kenetli; kontrol:
//     toplamsal bir model ayni sayilari veremez (fark olculur).
//  4) Ucus KARE HIZINDAN BAGIMSIZ (30 fps 2 s == 120 fps 2 s); kontrol: ayni
//     ADIM sayisi farkli dt ile AYNI SONUCU VERMEZ (yani dt gercekten kullanilir).
//     Ayrica sag tusu birakmak goruntuyu ZIPLATMAZ.
//  5) Odak: kutu gercekten cerceveye SIGAR (8 kose NDC icinde); kontrol: yari
//     mesafede SIGMAZ.
//  6) Eksen gorusleri: PlusY duz asagi, MinusY duz yukari bakar, hedef/uzaklik
//     korunur; kontrol: hizalamadan onceki bakis eksenel DEGILDI.
//  7) Ortografik `radius` mesafesinde perspektifle AYNI yuksekligi gosterir;
//     kontrol: 2*radius'ta AYRISIRLAR. Orto isinlari PARALEL, perspektifinki degil.
//  8) Kutu secim: kameranin ARKASINDAKI kutu asla secilmez (klasik w<0 hatasi);
//     kontrol: ayni kutu onde secilir. Tam icerme / kesisme AYRISIR, tampon
//     tavani (cap) sessizce tasmaz.
#include <cmath>
#include <cstdio>

#include "app/editor_camera.hpp"
#include "app/editor_overlay.hpp" // viewport_box_select (kutu secimin saf matematigi)
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
// Bir kareyi surer (yardimci: cagrilarin gurultusu kapiyi gizlemesin).
app::CameraInput mouse_drag(float dx, float dy, bool rmb, bool mmb = false, bool shift = false, bool alt = false) {
  app::CameraInput in;
  in.dx = dx;
  in.dy = dy;
  in.rmb = rmb;
  in.mmb = mmb;
  in.shift = shift;
  in.alt = alt;
  return in;
}
// Kutunun 8 kosesini izdusurup en kotu |NDC| bilesenini dondurur. 1'in altinda
// = tamami cerceveye sigiyor. Kose kameranin arkasindaysa (w <= 0) "sigmiyor"
// sayilir (buyuk deger).
float worst_ndc(const Mat4 &vp, const content::SceneBounds &b) {
  float worst = 0;
  for (int k = 0; k < 8; k++) {
    const Vec4 c = vp * Vec4{(k & 1) ? b.hi.x : b.lo.x, (k & 2) ? b.hi.y : b.lo.y, (k & 4) ? b.hi.z : b.lo.z, 1.0f};
    if (!(c.w > 1e-6f)) return 1e9f;
    const float nx = std::fabs(c.x / c.w), ny = std::fabs(c.y / c.w);
    if (nx > worst) worst = nx;
    if (ny > worst) worst = ny;
  }
  return worst;
}
content::SceneBounds box_at(Vec3 c, float half) { return {{c.x - half, c.y - half, c.z - half}, {c.x + half, c.y + half, c.z + half}}; }
} // namespace

ENGINE_TEST(editor_camera_orbit_rotates_and_pitch_clamps_at_both_ends) {
  app::EditorCamera c;
  const Vec3 t0 = c.target;
  const float r0 = c.radius, y0 = c.yaw;
  // Saga surukle: yaw AZALIR (dunyayi tutup cekiyorsun gelenegi), hedef ve
  // uzaklik DEGISMEZ.
  app::camera_update(c, mouse_drag(40.0f, 0.0f, true));
  std::printf("    [bilgi] yorunge: yaw %.4f -> %.4f (delta %.4f, beklenen %.4f), hedef oynadi mi %d, radius %.2f\n", (double)y0, (double)c.yaw,
              (double)(c.yaw - y0), (double)(-40.0f * c.sensitivity), (int)!nearly_equal(c.target, t0), (double)c.radius);
  CHECK(nearly_equal(c.yaw - y0, -40.0f * c.sensitivity, 1e-5f));
  CHECK(nearly_equal(c.target, t0) && nearly_equal(c.radius, r0));

  // Pitch kenedi IKI UCTA: 200 kare tam guclu surukleme.
  for (int i = 0; i < 200; i++) app::camera_update(c, mouse_drag(0.0f, 60.0f, true));
  const float hi = c.pitch;
  for (int i = 0; i < 400; i++) app::camera_update(c, mouse_drag(0.0f, -60.0f, true));
  const float lo = c.pitch;
  std::printf("    [bilgi] pitch kenedi: ust %.6f, alt %.6f, sinir +-%.6f\n", (double)hi, (double)lo, (double)c.pitch_limit);
  CHECK(nearly_equal(hi, c.pitch_limit, 1e-5f));
  CHECK(nearly_equal(lo, -c.pitch_limit, 1e-5f));
  // Kenette gorunum matrisi HALA saglam (tam 90 derecede look_at cross'u sifirlanir
  // ve satirlar sifira duser; 89.9 tam da bunun icin).
  const Mat4 v = app::camera_view(c);
  const Vec3 side{v.m[0][0], v.m[1][0], v.m[2][0]};
  const Vec3 f = app::camera_forward(c);
  std::printf("    [bilgi] kenette gorunum: sag vektor uzunlugu %.6f, ileri.y %.6f\n", (double)length(side), (double)f.y);
  CHECK(nearly_equal(length(side), 1.0f, 1e-3f));

  // KONTROL: arayuz fareyi yuttugunda (allow_mouse = false) HICBIR SEY degismez.
  app::EditorCamera d;
  const app::EditorCamera before = d;
  app::CameraInput blocked = mouse_drag(40.0f, 40.0f, true);
  blocked.allow_mouse = false;
  blocked.scroll = 3.0f;
  app::camera_update(d, blocked);
  std::printf("    [bilgi] KONTROL arayuz fareyi yutuyor: yaw %.4f (%.4f), radius %.2f (%.2f)\n", (double)d.yaw, (double)before.yaw, (double)d.radius,
              (double)before.radius);
  CHECK(d.yaw == before.yaw && d.pitch == before.pitch && d.radius == before.radius && nearly_equal(d.target, before.target));
  // KONTROL 2: tus basili degilken surukleme de bir sey yapmaz.
  app::camera_update(d, mouse_drag(40.0f, 40.0f, false));
  CHECK(d.yaw == before.yaw && nearly_equal(d.target, before.target));
}

ENGINE_TEST(editor_camera_pan_moves_target_perpendicular_to_view) {
  app::EditorCamera c;
  c.yaw = 0.9f;
  c.pitch = 0.35f;
  c.radius = 20.0f;
  const Vec3 f = app::camera_forward(c);
  const Vec3 t0 = c.target;
  const float yaw0 = c.yaw, pitch0 = c.pitch;
  app::camera_update(c, mouse_drag(30.0f, -18.0f, false, /*mmb*/ true));
  const Vec3 dm = c.target - t0;
  const float along = dot(dm, f), len = length(dm);
  std::printf("    [bilgi] kaydirma (orta tus): |delta| %.4f, bakis yonundeki bileseni %.6f (0 olmali), aci hatasi %.4f derece\n", (double)len,
              (double)along, (double)(std::asin(std::fabs(along) / (len > 0 ? len : 1)) * 180.0 / 3.14159265));
  CHECK(len > 0.01f);                            // gercekten hareket etti
  CHECK(std::fabs(along) < 1e-4f * (len + 1.0f)); // ve bakis yonunde HIC yol yok
  CHECK(c.yaw == yaw0 && c.pitch == pitch0);     // kaydirma dondurmez

  // Shift + sag tus AYNI kaydirmayi yapar (orta tussuz fareler icin).
  app::EditorCamera s;
  s.yaw = 0.9f; s.pitch = 0.35f; s.radius = 20.0f;
  app::camera_update(s, mouse_drag(30.0f, -18.0f, true, false, /*shift*/ true));
  const Vec3 ds = s.target - t0;
  std::printf("    [bilgi] Shift+sag tus kaydirmasi: delta (%.4f, %.4f, %.4f) orta tus (%.4f, %.4f, %.4f)\n", (double)ds.x, (double)ds.y, (double)ds.z,
              (double)dm.x, (double)dm.y, (double)dm.z);
  CHECK(nearly_equal(ds, dm, 1e-5f));

  // Olcek radius ile: iki kat uzaktan ayni surukleme iki kat yol alir (her
  // yakinlikta "ayni his").
  app::EditorCamera far;
  far.yaw = 0.9f; far.pitch = 0.35f; far.radius = 40.0f;
  app::camera_update(far, mouse_drag(30.0f, -18.0f, false, true));
  const float len_far = length(far.target - t0);
  std::printf("    [bilgi] radius olcegi: r=20 -> %.4f, r=40 -> %.4f, oran %.4f (2 olmali)\n", (double)len, (double)len_far, (double)(len_far / len));
  CHECK(nearly_equal(len_far / len, 2.0f, 1e-3f));

  // KONTROL: shift'siz sag tus hedefi OYNATMAZ (o donustur) — yani yukaridaki
  // "hedef gezdi" olcumu kaydirmanin kendisini olcuyor.
  app::EditorCamera rot;
  rot.yaw = 0.9f; rot.pitch = 0.35f; rot.radius = 20.0f;
  app::camera_update(rot, mouse_drag(30.0f, -18.0f, true));
  std::printf("    [bilgi] KONTROL sag tus (shift yok): hedef deltasi %.6f (0 olmali), yaw degisti %d\n", (double)length(rot.target - t0),
              (int)(rot.yaw != 0.9f));
  CHECK(nearly_equal(rot.target, t0));
  CHECK(rot.yaw != 0.9f);
}

ENGINE_TEST(editor_camera_dolly_is_multiplicative_and_clamped) {
  app::EditorCamera c;
  c.radius = 20.0f;
  app::CameraInput in;
  in.scroll = 1.0f;
  app::camera_update(c, in);
  const float r1 = c.radius;
  c.radius = 20.0f;
  in.scroll = 2.0f;
  app::camera_update(c, in);
  const float r2 = c.radius;
  std::printf("    [bilgi] yakinlik: 1 adim %.4f (20*q = %.4f), 2 adim %.4f (20*q^2 = %.4f)\n", (double)r1, (double)(20.0f * c.dolly_rate),
              (double)r2, (double)(20.0f * c.dolly_rate * c.dolly_rate));
  CHECK(nearly_equal(r1, 20.0f * c.dolly_rate, 1e-4f));
  CHECK(nearly_equal(r2, 20.0f * c.dolly_rate * c.dolly_rate, 1e-4f));
  // KONTROL: toplamsal bir model ayni iki sayiyi veremez — ikinci adimin
  // adim buyuklugu birinciden KUCUK olmali (carpimsalligin gozlenebilir izi).
  const float step1 = 20.0f - r1, step2 = r1 - r2;
  std::printf("    [bilgi] KONTROL toplamsal degil: 1. adim %.4f, 2. adim %.4f (kucuk olmali)\n", (double)step1, (double)step2);
  CHECK(step2 < step1 - 1e-4f);

  // Kenet iki ucta.
  app::EditorCamera z;
  for (int i = 0; i < 400; i++) { app::CameraInput s; s.scroll = 5.0f; app::camera_update(z, s); }
  const float rmin = z.radius;
  for (int i = 0; i < 800; i++) { app::CameraInput s; s.scroll = -5.0f; app::camera_update(z, s); }
  std::printf("    [bilgi] yakinlik kenedi: en yakin %.4f (min %.4f), en uzak %.4f (max %.4f)\n", (double)rmin, (double)z.min_radius,
              (double)z.radius, (double)z.max_radius);
  CHECK(nearly_equal(rmin, z.min_radius, 1e-4f));
  CHECK(nearly_equal(z.radius, z.max_radius, 1e-3f));

  // Alt + sag tus surukleme de yakinliktir: asagi surukle = uzaklas.
  app::EditorCamera a;
  a.radius = 20.0f;
  app::camera_update(a, mouse_drag(0.0f, 25.0f, true, false, false, /*alt*/ true));
  const float a_down = a.radius;
  a.radius = 20.0f;
  app::camera_update(a, mouse_drag(0.0f, -25.0f, true, false, false, true));
  std::printf("    [bilgi] Alt+sag tus: asagi %.4f (>20), yukari %.4f (<20), yaw degismedi %d\n", (double)a_down, (double)a.radius,
              (int)(a.yaw == app::EditorCamera{}.yaw));
  CHECK(a_down > 20.0f && a.radius < 20.0f);
  CHECK(a.yaw == app::EditorCamera{}.yaw); // Alt'li surukleme DONDURMEZ
}

ENGINE_TEST(editor_camera_fly_is_framerate_independent_and_leaves_no_jump) {
  // 2 saniyelik ileri ucus, iki farkli kare hizinda.
  auto fly = [](float dt, int steps) {
    app::EditorCamera c;
    c.yaw = 0.6f;
    c.pitch = 0.2f;
    app::CameraInput in;
    in.rmb = true;
    in.key_w = true;
    in.dt = dt;
    for (int i = 0; i < steps; i++) app::camera_update(c, in);
    return c.target;
  };
  const Vec3 p30 = fly(1.0f / 30.0f, 60);   // 2.0 s
  const Vec3 p120 = fly(1.0f / 120.0f, 240); // 2.0 s
  const Vec3 p_bad = fly(1.0f / 120.0f, 60); // 0.5 s — KONTROL
  std::printf("    [bilgi] ucus dt bagimsizligi: 30 fps (%.4f, %.4f, %.4f), 120 fps (%.4f, %.4f, %.4f), fark %.6f\n", (double)p30.x, (double)p30.y,
              (double)p30.z, (double)p120.x, (double)p120.y, (double)p120.z, (double)length(p30 - p120));
  CHECK(length(p30 - p120) < 1e-3f);
  std::printf("    [bilgi] KONTROL ayni ADIM sayisi farkli dt: fark %.4f (buyuk olmali — dt gercekten kullaniliyor)\n", (double)length(p30 - p_bad));
  CHECK(length(p30 - p_bad) > 1.0f);

  // Q/E DUNYA Y'sinde; Shift hizlandirir.
  app::EditorCamera up;
  up.pitch = 0.4f;
  app::CameraInput ie;
  ie.rmb = true;
  ie.key_e = true;
  ie.dt = 1.0f / 60.0f;
  const Vec3 t0 = up.target;
  app::camera_update(up, ie);
  const Vec3 du = up.target - t0;
  std::printf("    [bilgi] E (yukari): delta (%.4f, %.4f, %.4f) — yalniz Y\n", (double)du.x, (double)du.y, (double)du.z);
  CHECK(du.y > 0.0f && std::fabs(du.x) < 1e-6f && std::fabs(du.z) < 1e-6f);
  app::EditorCamera fast;
  fast.pitch = 0.4f;
  ie.shift = true;
  app::camera_update(fast, ie);
  std::printf("    [bilgi] Shift carpani: %.4f (beklenen %.2f)\n", (double)((fast.target.y - t0.y) / du.y), (double)fast.fast_mul);
  CHECK(nearly_equal((fast.target.y - t0.y) / du.y, fast.fast_mul, 1e-3f));

  // Bakis: GOZ SABIT kalir (yorunge gibi hedef etrafinda donmez).
  app::EditorCamera look;
  app::CameraInput il;
  il.rmb = true;
  il.key_w = true; // ucus mandalini kur
  il.dt = 0.0f;    // hareket yok, yalniz bakis
  app::camera_update(look, il);
  const Vec3 eye0 = app::camera_eye(look);
  il.key_w = false;
  il.dx = 50.0f;
  il.dy = 20.0f;
  app::camera_update(look, il);
  const Vec3 eye1 = app::camera_eye(look);
  std::printf("    [bilgi] ucus bakisi: goz kaymasi %.6f (0 olmali), yaw degisti %d\n", (double)length(eye1 - eye0), (int)(look.yaw != 0.7f));
  CHECK(length(eye1 - eye0) < 1e-3f);
  CHECK(look.yaw != 0.7f);

  // Sag tusu birakmak ZIPLATMAZ: goz ayni yerde, hedef tam `radius` kadar ONDE.
  app::EditorCamera j;
  app::CameraInput ij;
  ij.rmb = true;
  ij.key_w = true;
  ij.key_d = true;
  ij.dx = 12.0f;
  ij.dt = 1.0f / 60.0f;
  for (int i = 0; i < 30; i++) app::camera_update(j, ij);
  const Vec3 eye_fly = app::camera_eye(j);
  const Vec3 fwd_fly = app::camera_forward(j);
  CHECK(j.flying);
  app::CameraInput rel; // sag tus birakildi
  app::camera_update(j, rel);
  const Vec3 eye_after = app::camera_eye(j);
  const float d_target = length(j.target - eye_after);
  const float ahead = dot(normalize(j.target - eye_after), fwd_fly);
  std::printf("    [bilgi] ucustan yorungeye: goz ziplamasi %.6f (0), hedef uzakligi %.4f (radius %.4f), hedef onde mi %.6f (1), mandal %d\n",
              (double)length(eye_after - eye_fly), (double)d_target, (double)j.radius, (double)ahead, (int)j.flying);
  CHECK(length(eye_after - eye_fly) < 1e-4f);
  CHECK(nearly_equal(d_target, j.radius, 1e-3f));
  CHECK(ahead > 0.999f);
  CHECK(!j.flying);
  // Ve bundan sonraki sag tus suruklemesi YORUNGE gibi davranir: goz oynar,
  // hedef DURUR (kontrol: ucusta tam tersiydi).
  const Vec3 tgt_before = j.target;
  app::camera_update(j, mouse_drag(20.0f, 0.0f, true));
  std::printf("    [bilgi] birakinca yorunge: hedef deltasi %.6f (0), goz deltasi %.4f (>0)\n", (double)length(j.target - tgt_before),
              (double)length(app::camera_eye(j) - eye_after));
  CHECK(nearly_equal(j.target, tgt_before, 1e-5f));
  CHECK(length(app::camera_eye(j) - eye_after) > 0.01f);
}

ENGINE_TEST(editor_camera_focus_frames_bounds_half_radius_does_not) {
  app::EditorCamera c;
  c.yaw = 0.8f;
  c.pitch = 0.4f;
  const content::SceneBounds b = box_at({5.0f, 1.0f, -3.0f}, 2.0f);
  app::camera_focus(c, b);
  const float aspects[2] = {16.0f / 9.0f, 1.0f};
  for (int i = 0; i < 2; i++) {
    const Mat4 vp = app::camera_view_proj(c, aspects[i], 0.1f, 500.0f);
    const float w = worst_ndc(vp, b);
    std::printf("    [bilgi] odak: hedef (%.2f, %.2f, %.2f) radius %.3f, en-boy %.2f -> en kotu |NDC| %.4f (<1 sigar)\n", (double)c.target.x,
                (double)c.target.y, (double)c.target.z, (double)c.radius, (double)aspects[i], (double)w);
    CHECK(w < 1.0f);
  }
  CHECK(nearly_equal(c.target, Vec3{5.0f, 1.0f, -3.0f}, 1e-4f));
  // KONTROL: yari mesafede SIGMAZ (yoksa "sigiyor" olcumu her mesafede dogru
  // olurdu ve hicbir sey olcmezdi).
  app::EditorCamera half = c;
  half.radius = c.radius * 0.5f;
  const float wh = worst_ndc(app::camera_view_proj(half, 16.0f / 9.0f, 0.1f, 500.0f), b);
  std::printf("    [bilgi] KONTROL yari mesafe: radius %.3f -> en kotu |NDC| %.4f (>1 tasar)\n", (double)half.radius, (double)wh);
  CHECK(wh > 1.0f);

  // Hepsini cercevele: uc kutunun birlesimi.
  const content::SceneBounds many[3] = {box_at({-8.0f, 0.0f, 0.0f}, 1.0f), box_at({9.0f, 3.0f, 4.0f}, 2.0f), box_at({0.0f, -2.0f, -10.0f}, 1.5f)};
  app::EditorCamera all;
  app::camera_focus_all(all, many, 3);
  float worst = 0;
  for (int i = 0; i < 3; i++) {
    const float w = worst_ndc(app::camera_view_proj(all, 16.0f / 9.0f, 0.1f, 500.0f), many[i]);
    if (w > worst) worst = w;
  }
  std::printf("    [bilgi] hepsini cercevele: hedef (%.2f, %.2f, %.2f) radius %.3f, en kotu |NDC| %.4f\n", (double)all.target.x, (double)all.target.y,
              (double)all.target.z, (double)all.radius, (double)worst);
  CHECK(worst < 1.0f);
  // KONTROL: bos sahnede "F" kamerayi SICRATMAZ.
  const app::EditorCamera keep = all;
  app::camera_focus_all(all, many, 0);
  app::camera_focus_all(all, nullptr, 3);
  CHECK(nearly_equal(all.target, keep.target) && all.radius == keep.radius);
  // Dejenere kutu (nokta): cokmez, makul bir uzaklik verir.
  app::EditorCamera pt;
  app::camera_focus(pt, box_at({1.0f, 2.0f, 3.0f}, 0.0f));
  std::printf("    [bilgi] dejenere kutu: radius %.4f (sonlu ve > 0)\n", (double)pt.radius);
  CHECK(pt.radius > 0.0f && pt.radius < pt.max_radius);
}

ENGINE_TEST(editor_camera_align_axis_views_look_straight_down_and_up) {
  app::EditorCamera c;
  c.target = {2.0f, 1.0f, -4.0f};
  c.radius = 17.5f;
  const Vec3 f0 = app::camera_forward(c);
  std::printf("    [bilgi] KONTROL hizalamadan once ileri (%.3f, %.3f, %.3f) — eksenel DEGIL\n", (double)f0.x, (double)f0.y, (double)f0.z);
  CHECK(std::fabs(f0.y) < 0.99f && std::fabs(f0.x) > 0.01f && std::fabs(f0.z) > 0.01f);

  struct Want {
    app::CameraAxis a;
    const char *name;
    Vec3 f;
  };
  const Want want[6] = {
      {app::CameraAxis::PlusX, "+X (sag yan)", {-1, 0, 0}}, {app::CameraAxis::MinusX, "-X (sol yan)", {1, 0, 0}},
      {app::CameraAxis::PlusY, "+Y (ust)", {0, -1, 0}},     {app::CameraAxis::MinusY, "-Y (alt)", {0, 1, 0}},
      {app::CameraAxis::PlusZ, "+Z (on)", {0, 0, -1}},      {app::CameraAxis::MinusZ, "-Z (arka)", {0, 0, 1}},
  };
  for (int i = 0; i < 6; i++) {
    app::EditorCamera a = c;
    app::camera_align(a, want[i].a);
    const Vec3 f = app::camera_forward(a);
    const float d = dot(f, want[i].f);
    std::printf("    [bilgi] %s: ileri (%.4f, %.4f, %.4f) hedefle ic carpim %.6f; hedef korundu %d, radius korundu %d\n", want[i].name, (double)f.x,
                (double)f.y, (double)f.z, (double)d, (int)nearly_equal(a.target, c.target), (int)(a.radius == c.radius));
    CHECK(d > 0.999f);
    CHECK(nearly_equal(a.target, c.target) && a.radius == c.radius);
    // Gorunum matrisi bozulmadi (kenetin sebebi tam da bu).
    const Mat4 v = app::camera_view(a);
    CHECK(nearly_equal(length(Vec3{v.m[0][0], v.m[1][0], v.m[2][0]}), 1.0f, 1e-3f));
  }
}

ENGINE_TEST(editor_camera_ortho_matches_perspective_height_at_radius_only) {
  app::EditorCamera c;
  c.yaw = 0.0f;
  c.pitch = 0.0f;
  c.target = {0, 0, 0};
  c.radius = 20.0f;
  const float aspect = 16.0f / 9.0f;
  const Vec3 eye = app::camera_eye(c), f = app::camera_forward(c), up = app::camera_up(c);
  const float hh = c.radius * std::tan(c.fov_y * 0.5f);
  auto ndc_y = [&](const app::EditorCamera &cam, Vec3 p) {
    const Vec4 q = app::camera_view_proj(cam, aspect, 0.1f, 500.0f) * Vec4{p.x, p.y, p.z, 1.0f};
    return q.y / q.w;
  };
  app::EditorCamera o = c;
  o.proj = app::CameraProjection::Ortho;
  // `radius` mesafesinde cerceve yuksekliginin tepesi: iki kip de NDC -1 der.
  const Vec3 top_at_r = eye + f * c.radius + up * hh;
  const float py_r = ndc_y(c, top_at_r), oy_r = ndc_y(o, top_at_r);
  // 2*radius mesafesinde AYNI dunya yuksekligi: perspektifte kuculur (-0.5),
  // ortografikte kucultmez (-1) — kipler burada AYRISIR.
  const Vec3 top_at_2r = eye + f * (2.0f * c.radius) + up * hh;
  const float py_2r = ndc_y(c, top_at_2r), oy_2r = ndc_y(o, top_at_2r);
  std::printf("    [bilgi] izdusum: radius'ta perspektif %.5f / orto %.5f (esit); 2*radius'ta %.5f / %.5f (ayrisir)\n", (double)py_r, (double)oy_r,
              (double)py_2r, (double)oy_2r);
  CHECK(nearly_equal(py_r, -1.0f, 1e-4f));
  CHECK(nearly_equal(oy_r, -1.0f, 1e-4f));
  CHECK(nearly_equal(py_r, oy_r, 1e-4f));
  CHECK(std::fabs(py_2r - oy_2r) > 0.4f); // KONTROL: "her yerde ayni" degil
  CHECK(nearly_equal(py_2r, -0.5f, 1e-3f) && nearly_equal(oy_2r, -1.0f, 1e-4f));

  // Isinlar: ortografikte PARALEL (yon ayni, baslangic kayar), perspektifte degil.
  Vec3 o1, d1, o2, d2;
  app::camera_ray(o, aspect, 100.0f, 200.0f, 1600.0f, 900.0f, &o1, &d1);
  app::camera_ray(o, aspect, 1500.0f, 700.0f, 1600.0f, 900.0f, &o2, &d2);
  std::printf("    [bilgi] orto isinlari: yon farki %.6f (0), baslangic farki %.4f (>0)\n", (double)length(d1 - d2), (double)length(o1 - o2));
  CHECK(length(d1 - d2) < 1e-5f && length(o1 - o2) > 1.0f);
  Vec3 o3, d3, o4, d4;
  app::camera_ray(c, aspect, 100.0f, 200.0f, 1600.0f, 900.0f, &o3, &d3);
  app::camera_ray(c, aspect, 1500.0f, 700.0f, 1600.0f, 900.0f, &o4, &d4);
  std::printf("    [bilgi] KONTROL perspektif isinlari: yon farki %.4f (>0), baslangic farki %.6f (0)\n", (double)length(d3 - d4),
              (double)length(o3 - o4));
  CHECK(length(d3 - d4) > 0.1f && length(o3 - o4) < 1e-5f);
}

ENGINE_TEST(editor_box_select_never_takes_boxes_behind_the_camera) {
  // Kamera kokende, -Z'ye bakiyor (yaw = 0, pitch = 0 -> goz +Z'de).
  app::EditorCamera c;
  c.target = {0, 0, 0};
  c.yaw = 0.0f;
  c.pitch = 0.0f;
  c.radius = 10.0f; // goz (0, 0, 10)
  const app::ViewportRect view{0, 0, 800, 600};
  const Mat4 vp = app::camera_view_proj(c, 800.0f / 600.0f, 0.1f, 500.0f);
  // 0: tam onde (merkezde), 1: AYNI konumun kamera arkasindaki aynasi.
  const content::SceneBounds b[2] = {box_at({0, 0, 0}, 1.0f), box_at({0, 0, 20.0f}, 1.0f)};
  int32_t out[8];
  const uint32_t n_all = app::viewport_box_select(vp, b, 2, view, 0, 0, 800, 600, false, out, 8);
  std::printf("    [bilgi] tum ekrani kapsayan kutu: %u secildi (%d) — ondeki secilmeli, ARKADAKI ASLA\n", n_all, n_all ? out[0] : -1);
  CHECK(n_all == 1 && out[0] == 0);
  // KONTROL: arkadaki kutuyu ONE tasiyinca secilir — yani "secilmedi" olcumu
  // kutunun kendisinden degil, KAMERANIN ARKASINDA olmasindan geliyor.
  const content::SceneBounds mirrored[1] = {box_at({0, 0, -20.0f}, 1.0f)};
  const uint32_t n_front = app::viewport_box_select(vp, mirrored, 1, view, 0, 0, 800, 600, false, out, 8);
  std::printf("    [bilgi] KONTROL ayni kutu ONDE (z = -20): %u secildi\n", n_front);
  CHECK(n_front == 1);

  // Tam icerme / kesisme AYRISIR: ekranda tasan buyuk kutu kesisir ama icermez.
  const content::SceneBounds big[1] = {box_at({0, 0, 0}, 40.0f)};
  const uint32_t cross = app::viewport_box_select(vp, big, 1, view, 300, 200, 500, 400, false, out, 8);
  const uint32_t full = app::viewport_box_select(vp, big, 1, view, 300, 200, 500, 400, true, out, 8);
  std::printf("    [bilgi] kesisme %u / tam icerme %u (buyuk kutu, kucuk dikdortgen)\n", cross, full);
  CHECK(cross == 1 && full == 0);
  // Kucuk kutu kucuk dikdortgenin icinde: ikisi de secer (kontrol).
  const uint32_t cross2 = app::viewport_box_select(vp, b, 1, view, 300, 200, 500, 400, false, out, 8);
  const uint32_t full2 = app::viewport_box_select(vp, b, 1, view, 300, 200, 500, 400, true, out, 8);
  std::printf("    [bilgi] KONTROL kucuk kutu ayni dikdortgende: kesisme %u / tam icerme %u\n", cross2, full2);
  CHECK(cross2 == 1 && full2 == 1);

  // Ters surukleme (sagdan sola, asagidan yukari) ayni sonucu verir (normalize).
  const uint32_t rev = app::viewport_box_select(vp, b, 1, view, 500, 400, 300, 200, true, out, 8);
  CHECK(rev == full2);

  // Tampon tavani SESSIZ degil: uc kutu, cap 1 -> 1 yazilir; cap 8 -> 3;
  // out == nullptr -> yalniz sayar (cap yok sayilir).
  const content::SceneBounds three[3] = {box_at({-3, 0, 0}, 1.0f), box_at({0, 0, 0}, 1.0f), box_at({3, 0, 0}, 1.0f)};
  const uint32_t c1 = app::viewport_box_select(vp, three, 3, view, 0, 0, 800, 600, false, out, 1);
  const uint32_t c8 = app::viewport_box_select(vp, three, 3, view, 0, 0, 800, 600, false, out, 8);
  const uint32_t cn = app::viewport_box_select(vp, three, 3, view, 0, 0, 800, 600, false, nullptr, 0);
  std::printf("    [bilgi] tavan: cap 1 -> %u, cap 8 -> %u, yalniz say -> %u\n", c1, c8, cn);
  CHECK(c1 == 1 && c8 == 3 && cn == 3);
  // Dejenere girdiler cokmez.
  CHECK(app::viewport_box_select(vp, nullptr, 3, view, 0, 0, 10, 10, false, out, 8) == 0);
  CHECK(app::viewport_box_select(vp, three, 0, view, 0, 0, 10, 10, false, out, 8) == 0);
  CHECK(app::viewport_box_select(vp, three, 3, app::ViewportRect{0, 0, 0, 0}, 0, 0, 10, 10, false, out, 8) == 0);
}
