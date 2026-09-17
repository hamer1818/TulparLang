// L3 RENDERER — kumelenmis (clustered) isik atamasi, CPU tarafinda.
// Ekran tile x tile x derinlik dilimi; her kume icin 32-bit isik maskesi.
// Neden CPU: ilk oyun 8-16 dinamik isik ister (CIHAZ-MATRISI §1); bu olcekte
// CPU atamasi mikrosaniyelerdir, belirlenimlidir, compute + SSBO senkronu
// istemez ve Vulkan 1.1 cihazda ek ozellik gerektirmez. Isik sayisi buyurse
// ayni maskeler compute'ta uretilir (render pass ONCESI, zinciri bolmez, §8/10).
// Konservatif atama: kure gorunum-uzayi AABB'siyle projekte edilir; yakin
// duzlemin gerisine tasan kose tum ekrani isaretler (isik zaten kamerada).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

struct PointLight {
  Vec3 pos;
  float radius = 5.0f;
  Vec3 color{1, 1, 1};
  float intensity = 1.0f;
};

struct ClusterGrid {
  uint32_t x = 16, y = 9, z = 24;
  float znear = 0.1f, zfar = 200.0f;
  uint32_t count() const { return x * y * z; }
};

struct ClusterStats {
  uint32_t lights_visible = 0;  // en az bir kumeye giren
  uint32_t clusters_touched = 0; // maske != 0
  uint32_t max_lights_in_cluster = 0;
  // Stokastik yol olcumleri (kapaliyken evaluated == evaluated_full).
  uint32_t lights_evaluated = 0;      // SECILEN isik-kume ciftlerinin toplami
  uint32_t lights_evaluated_full = 0; // secim olmasaydi kac olurdu (referans)
  uint32_t clusters_sampled = 0;      // butcesi asilip ornekleme yapilan kume
  uint32_t lights_dropped = 0;        // ornekleme yuzunden degerlendirilmeyen isik-kume cifti
  float max_weight = 1.0f;            // en buyuk telafi agirligi (patlama gostergesi)
};

// --- Stokastik tile isiklandirma (PLAN EK A.1, HypeHype SIGGRAPH 2025) ------
// Dusuk segment cihazda bir tile'daki TUM isiklari degerlendirmek yerine onem
// agirlikli bir ALT KUME degerlendirilir; kalan enerji telafi agirligiyla geri
// verilir (yansiz tahmin edici).
//
// NEDEN CPU + tile basina, iki asamali per-pixel reservoir DEGIL: reservoir
// yeniden ornekleme piksel basina RNG ve (pratikte) bir compute gecisi ister —
// compute PLAN §8/10 ile YASAK — ve urettigi gurultu piksel frekansindadir,
// yani bir denoiser'a bagimlidir. Kume atamasi zaten CPU'da ve belirlenimli;
// secim TILE basina yapilinca tile icinde HIC gurultu olmaz, hata tile seklinde
// dusuk frekansli kucuk bir sapmaya doner.
//
// ZAMANSAL KARARLILIK (plan: "isik secimini temporal feedback ile stabilize et")
// uc mekanizmayla saglanir:
//   1. CAPA: her kumede en onemli `keep` isik HER KARE degerlendirilir. Enerjinin
//      buyuk kismi buradadir ve hic titremez.
//   2. KATMANLI (stratified) DONME: kalan "kuyruk" esit araliklarla ornege
//      alinir (adim = kuyruk/ornek) ve desen kare indeksiyle 1 kayar. Boylece
//      ceil(kuyruk/ornek) karede her kuyruk isigi ziyaret edilir; kareler arasi
//      degisim kuyrugun TOPLAM enerjisiyle sinirlidir (capa disinda kalan).
//   3. TILE BASINA: butun tile ayni kumeyi kullanir -> piksel frekansli
//      parildama (sparkle) yok; artik sinyal goz icin dusuk frekanslidir.
// Kapi bu titremeyi OLCER; kontrol (keep = 0, butce 1) titremeyi buyutur.
struct StochasticConfig {
  uint32_t budget = 0; // kume basina degerlendirilecek en cok isik (0 = KAPALI)
  uint32_t keep = 2;   // her kare tutulan en onemli isik sayisi (capa)
  uint32_t frame = 0;  // ornekleme deseni bu indeksle doner
  // Donme cozunurlugu: desen kare basina 1/phases kayar. BUYUK = ornekleme
  // noktalari CDF sinirini nadiren gecer, yani secim cogu karede AYNI kalir
  // (az titreme, yavas kapsama); KUCUK = tersi. 128'de olculdu: kare-arasi
  // ortalama degisim 2.0 bayt/kanal; 16'da 11.4 (ayni sahne).
  uint32_t phases = 128;
  // false = KONTROL kipi: ornekleme yapilir ama TELAFI AGIRLIGI uygulanmaz.
  // Kapinin "parlakligi koruyan sey telafi" iddiasini olcer; urunde hep true.
  bool compensate = true;
};

// Derinlik dilimi: log dagilim. Shader ile AYNI formul (cluster_params.xy).
void cluster_slice_params(const ClusterGrid &g, float *scale, float *bias);
uint32_t cluster_slice_of(const ClusterGrid &g, float view_depth); // view_depth > 0 (ileri)

// masks: g.count() eleman; sifirlanip doldurulur. proj: ciziminde kullanilan
// projeksiyon (Android on-dondurme dahil) — kumeler framebuffer uzayindadir.
// sc / stoch: stokastik yol. sc->budget > 0 ise masks SECILEN isiklara indirgenir
// ve stoch[2*i] = o kumede ornege alinan KUYRUK bitleri, stoch[2*i+1] = kuyrukta
// kac isik vardi (shader agirligi = stoch[2i+1] / popcount(stoch[2i])).
// sc == nullptr (varsayilan): maskeler ve istatistikler BIT BIT eski davranis.
void cluster_assign(const Mat4 &view, const Mat4 &proj, const PointLight *lights, uint32_t light_count,
                    const ClusterGrid &g, uint32_t *masks, ClusterStats *stats = nullptr,
                    const StochasticConfig *sc = nullptr, uint32_t *stoch = nullptr);

} // namespace tulpar::engine::renderer
