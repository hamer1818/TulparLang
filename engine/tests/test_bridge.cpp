// Tulpar kopru (bridge/engine_api): Tulpar betiginin gordugu C ABI'nin kendisi
// kosturulur — kurulum (headless), varlik kurma, fizik (zeminli oturur /
// zeminsiz duser: KONTROL), nesil etiketli id (silinen id hata loglar ve 0 doner;
// canli id loglamaz: KONTROL), kare disi HUD cagrisinin reddi, tus adi eslemesi,
// ve PPM ciktisinin gercekten SAHNE icermesi (bos kareyle piksel farki; iki bos
// kare arasinda 0 fark: KONTROL).
//
// Sonradan eklenen yetenekler, hepsi KONTROLLU:
//   sahne kuvveti   dinamik govde durtuyle yer degistirir; KONTROL: sabit govde
//                   kimildamaz ve cagri hata sayacini bir artirir.
//   bolum gecisi    eng_scene_unload sonrasi sahne varligi 0, govde duser, cizim
//                   azalir; ardindan yeniden yukleme basarili (govde geri gelir).
//   animasyon       pozlu cizim statik cizimden FARKLI piksel verir; KONTROL: ayni
//                   poz iki kez cizilince fark 0.
//   ses             calan ses sayaci ve tepe deger artar; KONTROL: durdurunca
//                   ikisi de sifirlanir. Cihaz yoksa GORUNUR atlanir.
//
// Tek surecte tek motor ornegi var (global baglam): bu dosya init/shutdown'u
// BIR kez yapar ve tum kapilar o oturumun icinde kosar.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include "bridge/engine_api.h"
#include "content/scene.hpp"
#include "content/scene_blob.hpp"
#include "core/memory/arena.hpp"
#include "platform/thread.hpp"
#include "rhi/vk_api.hpp"
#if defined(_WIN32)
#include <cstdlib>
// Windows CRT'sinde setenv/unsetenv yok; _putenv_s ayni isi gorur
// ("DEGISKEN=" bos deger = silme).
static inline int setenv(const char *k, const char *v, int) { return _putenv_s(k, v ? v : ""); }
static inline int unsetenv(const char *k) { return _putenv_s(k, ""); }
#endif

#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
// PPM (P6) oku: basligi atla, pikselleri say. Donus: piksel sayisi (0 = hata).
uint32_t read_ppm(const char *path, uint8_t *out, uint32_t max_px, uint32_t *w_out, uint32_t *h_out) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return 0;
  char magic[3] = {0};
  unsigned w = 0, h = 0, maxv = 0;
  if (std::fscanf(f, "%2s %u %u %u", magic, &w, &h, &maxv) != 4 || std::strcmp(magic, "P6") != 0 || maxv != 255) { std::fclose(f); return 0; }
  std::fgetc(f); // tek bosluk
  const uint32_t n = w * h;
  if (n == 0 || n > max_px) { std::fclose(f); return 0; }
  const size_t got = std::fread(out, 3, n, f);
  std::fclose(f);
  if (w_out) *w_out = w;
  if (h_out) *h_out = h;
  return got == n ? n : 0;
}
uint32_t diff_px(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t d = 0;
  for (uint32_t i = 0; i < n; i++) {
    const int dr = a[i * 3] - b[i * 3], dg = a[i * 3 + 1] - b[i * 3 + 1], db = a[i * 3 + 2] - b[i * 3 + 2];
    if (dr * dr + dg * dg + db * db > 64) d++;
  }
  return d;
}
void run_frames(int n) {
  for (int i = 0; i < n; i++) { teng_frame_begin(); teng_frame_end(); }
}
constexpr uint32_t kW = 320, kH = 200;
// Varlik dizini: kaynak yollari SAHNE DOSYASININ yanindan cozulur, bu yuzden
// gecici blob da oraya yazilir (/tmp'ye yazsak glTF'ler bulunamazdi).
const char *assets_dir() {
  const char *d = std::getenv("TULPAR_ENGINE_ASSETS");
  if (d && *d) return d;
  static char buf[512];
  std::snprintf(buf, sizeof buf, "%s/tests/assets", ENGINE_SOURCE_DIR);
  return buf;
}
} // namespace

ENGINE_TEST(bridge_runs_a_scripted_game_headless) {
  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { skip("Vulkan loader yok"); return; }
  char shot_scene[512], shot_empty[512], shot_empty2[512];
  tmp_template(shot_scene, sizeof shot_scene, "kopru_sahne");
  tmp_template(shot_empty, sizeof shot_empty, "kopru_bos");
  tmp_template(shot_empty2, sizeof shot_empty2, "kopru_bos2");
  char *shots[3] = {shot_scene, shot_empty, shot_empty2};
  for (int i = 0; i < 3; i++) { const int fd = mkstemp(shots[i]); if (fd >= 0) close(fd); }

  teng_log_level(1); // kapi ciktisi sessiz kalsin; HATA yine gorunur
  teng_set_headless(100000, nullptr); // kare sinirina takilma: donguyu test surer
  const int ok = teng_init("kopru kapisi", kW, kH);
  if (!ok) { std::printf("    [bilgi] kurulum: %s\n", teng_last_error()); skip("Vulkan cihazi/kurulum yok"); return; }
  CHECK(teng_headless() == 1);
  CHECK(teng_running() == 1);
  CHECK(teng_width() == (int)kW && teng_height() == (int)kH);
  std::printf("    [bilgi] GPU: %s, %dx%d\n", teng_gpu_name(), teng_width(), teng_height());
  const int err0 = teng_error_count();

  // --- 1) Bos kare (kontrol icin): hicbir varlik yok ---
  teng_camera(0, 4, 10, 0, 0.5, 0);
  run_frames(2);
  CHECK(teng_screenshot(shot_empty) == 1);
  run_frames(1);
  CHECK(teng_screenshot(shot_empty2) == 1);

  // --- 2) Sahne: zemin + dusen kutu + kure + isik ---
  const int zemin = teng_spawn_ground(8.0, 0xCED4DAFF);
  const int kutu = teng_spawn_box(0, 4.0, 0, 0.5, 0.5, 0.5, 1, 0xE63946FF);
  const int kure = teng_spawn_sphere(2.0, 1.0, 0, 0.5, 0, 0x2A9D8FFF);
  const int isik = teng_spawn_light(0, 3, 2, 0xF77F00FF, 3.0, 8.0);
  CHECK(zemin > 0 && kutu > 0 && kure > 0 && isik > 0);
  CHECK(teng_count() == 4);
  CHECK(teng_alive(kutu) == 1);
  CHECK(teng_is_dynamic(kutu) == 1 && teng_is_dynamic(kure) == 0);
  // Ayni yuva, farkli varlik: id'ler cakismaz.
  CHECK(kutu != kure && kutu != zemin);

  // Fizik: 2 s (120 kare) sonra kutu zemine oturur (yarim kenar 0.5 -> y ~0.5).
  run_frames(120);
  const double y_zeminli = teng_y(kutu);
  CHECK(y_zeminli > 0.3 && y_zeminli < 0.8);
  CHECK(teng_body_count() == 3); // zemin + kutu + kure (isik govdesiz)
  CHECK(teng_light_count() == 1);
  CHECK(teng_draw_count() == 3); // zemin + kutu + kure (isik cizim degil)
  CHECK(teng_screenshot(shot_scene) == 1);

  // --- 3) KONTROL: zemin silinince ayni kutu duser ---
  teng_despawn(zemin);
  CHECK(teng_alive(zemin) == 0);
  CHECK(teng_count() == 3);
  teng_set_pos(kutu, 0, 4.0, 0); // isinla: govde yeniden kurulur, hiz sifir
  CHECK(std::fabs(teng_vy(kutu)) < 0.001);
  run_frames(120);
  const double y_zeminsiz = teng_y(kutu);
  CHECK(y_zeminsiz < -3.0);
  std::printf("    [bilgi] kutu y: zeminli %.3f, zeminsiz %.3f (2 s)\n", y_zeminli, y_zeminsiz);

  // --- 4) Hiz / itme ---
  teng_set_velocity(kutu, 0, 0, 0);
  teng_set_pos(kutu, 0, 2.0, 0);
  teng_impulse(kutu, 3.0, 0, 0);
  CHECK(teng_vx(kutu) > 2.5);
  run_frames(30);
  CHECK(teng_x(kutu) > 0.5); // +x'e gitti

  // --- 4.5) Derlenmis sahne (.sahneb): editor -> engine_sahnec -> oyun yolu ---
  // Sahne metni burada derlenir (engine_sahnec'in yaptigi is), varlik dizinine
  // yazilir ve KOPRU uzerinden yuklenir: modeller, isiklar ve govdeler gelir.
  // Kapi bayraklari: kasitli hata sayisi hangi bolumlerin kostuguna bagli.
  bool sahne_kapisi = false, anim_kapisi = false, ses_kapisi = false;
  int ortam_hatasi = 0; // ortamdan gelen (kasitsiz) hatalar: cihaz/kaynak yok
  {
    static SystemArena arena;
    if (arena.capacity() == 0) arena.reserve(64u << 20, "bridge_scene_test");
    char src[768], blob[768];
    std::snprintf(src, sizeof src, "%s/editor.sahne", assets_dir());
    std::snprintf(blob, sizeof blob, "%s/_kopru_test.sahneb", assets_dir());
    static content::SceneDesc desc;
    content::SceneError serr{};
    const bool authored = content::scene_load(arena, src, &desc, &serr);
    if (!authored) std::printf("    [bilgi] sahne kaynagi yok (%s): %s\n", src, serr.msg);
    if (authored && content::scene_blob_save(arena, desc, blob, &serr)) {
      sahne_kapisi = true;
      const int draws_before = teng_draw_count();
      const int bodies_before = teng_body_count();
      const int errs_scene = teng_error_count();
      CHECK(teng_scene_load(blob) == 1);
      CHECK(teng_error_count() == errs_scene); // KONTROL: basarili yukleme hata uretmez
      CHECK(teng_scene_count() == (int)desc.entity_count);
      const int kup = teng_scene_find("kup_dusen");
      const int duvar = teng_scene_find("duvar_sabit");
      CHECK(kup >= 0 && duvar >= 0);
      CHECK(teng_scene_find("olmayan_varlik") == -1);
      CHECK(std::strcmp(teng_scene_name(kup), "kup_dusen") == 0);
      if (kup >= 0 && duvar >= 0) {
        const double kup_y0 = teng_scene_y(kup), duvar_y0 = teng_scene_y(duvar);
        run_frames(90);
        const double kup_y1 = teng_scene_y(kup), duvar_y1 = teng_scene_y(duvar);
        // Dinamik sahne govdesi sim'den gelir (duser), sabit olan yazar konumunda kalir.
        CHECK(kup_y1 < kup_y0 - 1.0);
        CHECK(duvar_y1 == duvar_y0);
        std::printf("    [bilgi] sahne: %d varlik, kup_dusen y %.2f -> %.2f, duvar sabit %.2f\n", teng_scene_count(), kup_y0, kup_y1, duvar_y1);
      }
      // Sahne cizimleri ve govdeleri kopruye eklendi.
      run_frames(1);
      CHECK(teng_draw_count() > draws_before);
      CHECK(teng_body_count() > bodies_before);
      // KONTROL: ikinci yukleme ve olmayan dosya reddedilir (her biri bir hata).
      const int errs_dup = teng_error_count();
      CHECK(teng_scene_load(blob) == 0);
      CHECK(teng_error_count() == errs_dup + 1);
      CHECK(teng_scene_load("/olmayan/dizin/x.sahneb") == 0);
      CHECK(teng_error_count() == errs_dup + 2);
      // Sinir disi sahne dizini de hata verir, 0 doner.
      CHECK(teng_scene_x(9999) == 0.0);
      CHECK(teng_error_count() == errs_dup + 3);

      // --- 4.6) SAHNE VARLIGINA KUVVET: dinamik govde itilir ---
      // (Sahne bugune kadar yalniz okunuyordu; kuvvet olmadan bolum icinde
      // oynanis yapilamiyordu.) KONTROL: sabit govde kimildamaz + hata sayar.
      if (kup >= 0 && duvar >= 0) {
        CHECK(teng_scene_is_dynamic(kup) == 1);
        CHECK(teng_scene_is_dynamic(duvar) == 0);
        teng_scene_set_velocity(kup, 0, 0, 0);
        CHECK(std::fabs(teng_scene_vx(kup)) < 0.001);
        const double kx0 = teng_scene_x(kup);
        teng_scene_impulse(kup, 4.0, 0, 0);
        CHECK(teng_scene_vx(kup) > 3.0);
        run_frames(30);
        const double kx1 = teng_scene_x(kup);
        CHECK(kx1 > kx0 + 0.5); // durtu tasidi
        // KONTROL: sabit govde — konum AYNI kalir, cagri bir hata uretir.
        const double dx0 = teng_scene_x(duvar);
        const int errs_static = teng_error_count();
        teng_scene_impulse(duvar, 9.0, 0, 0);
        CHECK(teng_error_count() == errs_static + 1);
        run_frames(30);
        CHECK(teng_scene_x(duvar) == dx0);
        std::printf("    [bilgi] sahne kuvveti: kup x %.2f -> %.2f (durtu 4.0), duvar sabit %.2f (durtu yok sayildi)\n", kx0, kx1, dx0);
      }

      // --- 4.7) BOLUM GECISI: bosalt -> sayaclar duser -> yeniden yukle ---
      {
        run_frames(1);
        const int draws_scene = teng_draw_count();
        const int bodies_scene = teng_body_count();
        CHECK(teng_scene_loaded() == 1);
        CHECK(teng_scene_unload() == 1);
        CHECK(teng_scene_loaded() == 0);
        CHECK(teng_scene_count() == 0);
        const int bodies_empty = teng_body_count();
        CHECK(bodies_empty < bodies_scene); // sahne govdeleri fizikten cikti
        run_frames(1);
        const int draws_empty = teng_draw_count();
        CHECK(draws_empty < draws_scene); // cizim de durdu
        // KONTROL: bos sahnede bosaltma hata verir (sessiz gecmez).
        const int errs_unload = teng_error_count();
        CHECK(teng_scene_unload() == 0);
        CHECK(teng_error_count() == errs_unload + 1);
        // Bolum gecisi: ayni blob yeniden yuklenir (birinci yuklemede reddediliyordu).
        CHECK(teng_scene_load(blob) == 1);
        CHECK(teng_scene_loaded() == 1);
        CHECK(teng_scene_count() == (int)desc.entity_count);
        run_frames(1);
        CHECK(teng_body_count() == bodies_scene);
        CHECK(teng_draw_count() == draws_scene);
        std::printf("    [bilgi] bolum gecisi: cizim %d -> %d -> %d, govde %d -> %d -> %d\n", draws_scene, draws_empty, teng_draw_count(),
                    bodies_scene, bodies_empty, teng_body_count());
      }
      unlink(blob);
    } else if (authored) {
      std::printf("    [bilgi] blob yazilamadi (%s): %s\n", blob, serr.msg);
      CHECK(false);
    } else skip("sahne varligi yok (editor.sahne)");
  }

  // --- 5) Nesil etiketli id: silinen id HATA loglar ve 0 doner ---
  const int errs_before = teng_error_count();
  CHECK(teng_x(kure) != 0.0 || teng_y(kure) != 0.0); // canli id: sorun yok
  CHECK(teng_error_count() == errs_before);          // KONTROL: canli id hata URETMEZ
  teng_despawn(kure);
  CHECK(teng_alive(kure) == 0);
  const double olu_x = teng_x(kure);
  CHECK(olu_x == 0.0);
  CHECK(teng_error_count() == errs_before + 1); // tam bir hata (sessiz yutma yok)
  teng_set_velocity(kure, 1, 1, 1);             // olu id: bir hata daha
  CHECK(teng_error_count() == errs_before + 2);

  // --- 6) Kare disi HUD cagrisi reddedilir (kare icinde kabul) ---
  const int errs_hud = teng_error_count();
  teng_text("kare disi", 10, 10, 1.0, 0xFFFFFFFF);
  CHECK(teng_error_count() == errs_hud + 1);
  teng_frame_begin();
  teng_text("kare ici", 10, 10, 1.0, 0xFFFFFFFF);
  teng_rect(10, 40, 100, 20, 0x000000AA);
  teng_frame_end();
  CHECK(teng_error_count() == errs_hud + 1); // KONTROL: kare icinde hata yok

  // --- 7) Tus adi eslemesi: bilinmeyen ad hata, bilinen ad sessiz ---
  const int errs_key = teng_error_count();
  CHECK(teng_key_down("W") == 0); // headless: girdi yok ama ad gecerli
  CHECK(teng_key_down("SPACE") == 0);
  CHECK(teng_key_down("7") == 0);
  CHECK(teng_error_count() == errs_key); // KONTROL: gecerli adlar hata uretmez
  CHECK(teng_key_down("YOKTUS") == 0);
  CHECK(teng_error_count() == errs_key + 1);

  // --- 8) Kare sayaci ve zaman ilerledi ---
  CHECK(teng_frame() > 250);
  CHECK(teng_time() > 4.0); // headless dt = 1/60
  CHECK(teng_dt() > 0.0);

  // --- 9) Piksel: sahne karesi bos kareden FARKLI, iki bos kare AYNI (kontrol) ---
  static uint8_t px_scene[kW * kH * 3], px_empty[kW * kH * 3], px_empty2[kW * kH * 3];
  uint32_t w = 0, h = 0;
  const uint32_t n1 = read_ppm(shot_scene, px_scene, kW * kH, &w, &h);
  const uint32_t n2 = read_ppm(shot_empty, px_empty, kW * kH, nullptr, nullptr);
  const uint32_t n3 = read_ppm(shot_empty2, px_empty2, kW * kH, nullptr, nullptr);
  CHECK(n1 > 0 && n1 == n2 && n2 == n3 && w == kW && h == kH);
  if (n1 > 0 && n1 == n2 && n2 == n3) {
    const uint32_t d_scene = diff_px(px_scene, px_empty, n1), d_ctrl = diff_px(px_empty, px_empty2, n1);
    std::printf("    [bilgi] piksel farki: sahne-bos %u, bos-bos %u (%ux%u)\n", d_scene, d_ctrl, w, h);
    // Sanal GPU'da (Apple Paravirtual, CI macOS) sahne karesi bos cikiyor —
    // ayni sinif test.hpp'de 2026-09-14'te belgelendi. ATLAMA YALNIZ PIKSEL
    // BLOGUNA: kaparin geri kalani (yasam dongusu, varlik, fizik, girdi, hata
    // sayaci, kare/zaman) macOS'ta da KOSUYOR. Butun kapiyi atlamak o kapsami
    // bedavaya kaybetmek olurdu.
    if (test::gpu_is_virtual(teng_gpu_name())) {
      skip("sanal GPU (Apple Paravirtual, CI macOS): kaparin PIKSEL blogu gercek cihazda olculur");
    } else {
      CHECK(d_scene > 1000);
      CHECK(d_ctrl == 0);
    }
  }
  unlink(shot_scene); unlink(shot_empty); unlink(shot_empty2);

  // --- 10) MODEL ANIMASYONU: pozlu cizim statikten FARKLI piksel verir -------
  // KONTROL: ayni poz iki kez cizilince fark 0 (yazici sabit cikti veriyor).
  // Sahne bosaltilir ve butun kopru varliklari silinir: kare yalniz modeli
  // icersin, yoksa dusen kutu farki kendi basina uretir (yanlis yesil).
  {
    // KONTROL (once): animasyon model OLMAYAN varlikta hata verir.
    const int errs_anim0 = teng_error_count();
    teng_set_anim(kutu, 0, 1.0, 1);
    CHECK(teng_error_count() == errs_anim0 + 1);

    if (teng_scene_loaded()) teng_scene_unload();
    teng_despawn(kutu);
    teng_despawn(isik);
    CHECK(teng_count() == 0);
    char model_path[768];
    std::snprintf(model_path, sizeof model_path, "%s/skin_tube.gltf", assets_dir());
    const int model = teng_load_model(model_path);
    if (model < 0) {
      ortam_hatasi++; // yuklenemeyen kaynak = ortam hatasi, kasitli degil
      skip("skin_tube.gltf yok — animasyon kapisi kosmadi");
    } else {
      anim_kapisi = true;
      CHECK(teng_model_clip_count(model) == 1);
      CHECK(std::strcmp(teng_model_clip_name(model, 0), "bend") == 0);
      const double sure = teng_model_clip_duration(model, 0);
      CHECK(sure > 0.5);
      // KONTROL: olmayan klip hata verir ve 0 doner.
      const int errs_clip = teng_error_count();
      CHECK(teng_model_clip_duration(model, 9) == 0.0);
      CHECK(teng_error_count() == errs_clip + 1);

      char shot_stat[512], shot_stat2[512], shot_anim[512];
      tmp_template(shot_stat, sizeof shot_stat, "kopru_stat");
      tmp_template(shot_stat2, sizeof shot_stat2, "kopru_stat2");
      tmp_template(shot_anim, sizeof shot_anim, "kopru_anim");
      char *ashots[3] = {shot_stat, shot_stat2, shot_anim};
      for (int i = 0; i < 3; i++) { const int fd = mkstemp(ashots[i]); if (fd >= 0) close(fd); }

      const int ent = teng_spawn_model(model, 0, 0, 0, 1.0, 0xFFFFFFFF);
      CHECK(ent > 0);
      teng_camera(0, 1.2, 3.2, 0, 1.0, 0);
      run_frames(2);
      CHECK(teng_screenshot(shot_stat) == 1);
      run_frames(1);
      CHECK(teng_screenshot(shot_stat2) == 1); // KONTROL karesi: ayni (statik) poz
      // Klip ata: klibin ortasina kadar ilerlet (headless dt = 1/60).
      teng_set_anim(ent, 0, 1.0, 1);
      CHECK(teng_anim_time(ent) == 0.0);
      run_frames(30);
      const double t_anim = teng_anim_time(ent);
      CHECK(t_anim > 0.4 && t_anim < 0.6);
      CHECK(teng_anim_done(ent) == 0); // dongulu klip bitmez
      CHECK(teng_screenshot(shot_anim) == 1);

      static uint8_t px_s[kW * kH * 3], px_s2[kW * kH * 3], px_a[kW * kH * 3];
      const uint32_t m1 = read_ppm(shot_stat, px_s, kW * kH, nullptr, nullptr);
      const uint32_t m2 = read_ppm(shot_stat2, px_s2, kW * kH, nullptr, nullptr);
      const uint32_t m3 = read_ppm(shot_anim, px_a, kW * kH, nullptr, nullptr);
      CHECK(m1 > 0 && m1 == m2 && m2 == m3);
      if (m1 > 0 && m1 == m2 && m2 == m3) {
        const uint32_t d_anim = diff_px(px_a, px_s, m1), d_ctrl = diff_px(px_s, px_s2, m1);
        std::printf("    [bilgi] animasyon: klip \"%s\" %.2f s, t=%.2f; piksel farki pozlu-statik %u, statik-statik %u\n",
                    teng_model_clip_name(model, 0), sure, t_anim, d_anim, d_ctrl);
        if (test::gpu_is_virtual(teng_gpu_name())) {
          skip("sanal GPU (Apple Paravirtual, CI macOS): animasyon PIKSEL karsilastirmasi gercek cihazda");
        } else {
          CHECK(d_anim > 100);
          CHECK(d_ctrl == 0);
        }
      }
      // Tek sefer calan klip: sure dolunca biter (dongulude bitmiyordu).
      teng_set_anim(ent, 0, 4.0, 0);
      run_frames(60);
      CHECK(teng_anim_done(ent) == 1);
      teng_set_anim(ent, -1, 1.0, 0); // statige don (hata degil)
      CHECK(teng_anim_time(ent) == 0.0);
      teng_despawn(ent);
      unlink(shot_stat); unlink(shot_stat2); unlink(shot_anim);
    }
  }

  // --- 11) SES: calan ses sayaci ve tepe deger; KONTROL: durdurunca sifir ----
  {
    int a_ok = teng_audio_open(0, 0);
    bool null_backend = false;
    if (!a_ok) {
      // Gercek cihaz yok (CI konteyneri): null arka uc POZITIF KONTROL olarak
      // kosar — callback thread'i gercekten calisir, sayaclar gercektir.
      ortam_hatasi++;
      setenv("TULPAR_ENGINE_AUDIO_NULL", "1", 1);
      a_ok = teng_audio_open(0, 0);
      null_backend = a_ok != 0;
      if (!a_ok) ortam_hatasi++;
      unsetenv("TULPAR_ENGINE_AUDIO_NULL");
    }
    if (!a_ok) {
      skip("ses cihazi yok (gercek de null arka uc da acilmadi)");
    } else {
      ses_kapisi = true;
      std::printf("    [bilgi] ses: %s%s\n", teng_audio_backend(), null_backend ? "  (null arka uc)" : "");
      CHECK(teng_audio_ok() == 1);
      const int klip = teng_audio_tone(440.0, 1.0);
      CHECK(klip >= 0);
      CHECK(teng_audio_tone(440.0, 1.0) == klip); // ayni ton = ayni tutamac (arena buyumez)
      // KONTROL: hicbir ses calmiyorken sayac ve tepe 0.
      platform::thread_sleep_us(150000);
      CHECK(teng_audio_playing() == 0);
      CHECK(teng_audio_peak() == 0.0);
      const int ses = teng_audio_play(klip, 0.5, 1);
      CHECK(ses != 0);
      platform::thread_sleep_us(250000);
      const int calan = teng_audio_playing();
      const double tepe = teng_audio_peak();
      CHECK(calan >= 1);
      CHECK(tepe > 0.05);
      // KONTROL: durdurulunca ikisi de sifira doner.
      teng_audio_stop(ses);
      platform::thread_sleep_us(250000);
      const int calan_sonra = teng_audio_playing();
      const double tepe_sonra = teng_audio_peak();
      std::printf("    [bilgi] ses kapisi: calan %d tepe %.2f -> durdurunca calan %d tepe %.2f\n", calan, tepe, calan_sonra, tepe_sonra);
      CHECK(calan_sonra == 0);
      CHECK(tepe_sonra == 0.0);
      // Gecersiz argumanlar: iki kasitli hata.
      const int errs_audio = teng_error_count();
      CHECK(teng_audio_play(999, 1.0, 0) == 0);
      CHECK(teng_audio_tone(-5.0, 1.0) == -1);
      CHECK(teng_error_count() == errs_audio + 2);
      teng_audio_master(0.5); // ana seviye: calan ses yokken de kabul edilir
      teng_audio_stop_all();
      teng_audio_close();
      CHECK(teng_audio_ok() == 0);
      // Cihaz kapaliyken cagri COKMEZ: hata loglanir, cagri yok sayilir.
      const int errs_off = teng_error_count();
      CHECK(teng_audio_play(klip, 1.0, 0) == 0);
      CHECK(teng_audio_playing() == 0);
      CHECK(teng_error_count() == errs_off + 1);
    }
  }

  // --- 12) Kapanis: kasitli hata sayisi (kosan kapilara gore) ----------------
  // Ortamdan gelen hatalar (ses cihazi yok, kaynak yok) ayri sayilir; onlar
  // kasitli degil ve makineye gore degisir.
  int beklenen = 2 /*olu id*/ + 1 /*kare disi HUD*/ + 1 /*gecersiz tus adi*/ + 1 /*model olmayan varlikta animasyon*/;
  if (sahne_kapisi) beklenen += 3 /*ikinci yukleme, olmayan dosya, sinir disi dizin*/ + 1 /*sabit govdeye durtu*/ + 1 /*bos sahnede bosaltma*/;
  if (anim_kapisi) beklenen += 1 /*olmayan klip*/;
  if (ses_kapisi) beklenen += 3 /*olmayan klip, negatif frekans, kapali cihazda cal*/;
  const int errs_total = teng_error_count() - err0 - ortam_hatasi;
  std::printf("    [bilgi] kasitli hata %d/%d (sahne kapisi %d, animasyon %d, ses %d), ortam hatasi %d, uyari %d\n", errs_total, beklenen,
              (int)sahne_kapisi, (int)anim_kapisi, (int)ses_kapisi, ortam_hatasi, teng_warning_count());
  CHECK(errs_total == beklenen);
  teng_close();
  CHECK(teng_running() == 0);
  teng_shutdown();
}
