// L4 SIMULATION — Animasyon: iskelet + sikistirilmis klip + poz degerlendirme.
// Plan §1.6 "ACL sinifi sikistirma": ACL'nin temel fikirleri kendi kodumuzda
// (yazma-bagla kurali burada bilerek asildi: ACL 30k satir, bize gereken
// alt kume ~400 satir ve VERI FORMATI bizim olmali — sahne derleyicisi
// ureteecek):
//   * sabit iz eleme: klip boyunca degismeyen iz bir kez saklanir
//   * aralik indirgeme + niceleme: ceviri/olcek iz basina [min,max] icinde
//     16-bit; donus "en kucuk uc" kodlamasiyla 3x16-bit (w yeniden kurulur)
//   * sabit ornekleme hizi, dogrusal (ceviri/olcek) ve nlerp (donus) ara deger
// Bake (`ClipBuilder`) ayirma yapar (sahne derleyicisi); runtime `sample()`
// sifir ayirma, job'lanabilir (her ornek bagimsiz).
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::sim {

struct Joint {
  int16_t parent = -1; // -1 = kok
  Vec3 bind_t{};
  Quat bind_r{};
  Vec3 bind_s{1, 1, 1};
};

struct Skeleton {
  const Joint *joints = nullptr;
  uint32_t count = 0;
};

// Ham klip (bake girdisi): eklem basina ornek dizileri (sabit hizda).
struct RawTrack {
  const Vec3 *t = nullptr; // [samples]
  const Quat *r = nullptr; // [samples]
  const Vec3 *s = nullptr; // [samples] (null = 1,1,1)
};
struct RawClip {
  const RawTrack *tracks = nullptr; // [joints]
  uint32_t joints = 0;
  uint32_t samples = 0;
  float sample_rate = 30.0f;
};

// Sikistirilmis klip blob'u (bake ciktisi). Duz bellek, isaretci yok:
// diske yazilip mmap'lenebilir.
struct ClipHeader {
  uint32_t magic;        // 'TCLP'
  uint32_t joints;
  uint32_t samples;
  float sample_rate;
  float duration_s;
  uint32_t track_desc_off; // TrackDesc[joints]
  uint32_t const_off;      // sabit degerler (float dizisi)
  uint32_t anim_off;       // animasyonlu iz verisi (uint16 dizisi, [sample][iz][bilesen])
  uint32_t total_bytes;
};
struct TrackDesc {
  uint8_t t_const, r_const, s_const; // 1 = sabit (const_off'ta), 0 = animasyonlu
  uint8_t pad;
  uint32_t t_idx, r_idx, s_idx;      // sabitse const float ofseti, degilse anim iz indeksi
  float t_min[3], t_ext[3];          // aralik indirgeme (animasyonlu ceviri)
  float s_min[3], s_ext[3];
};

struct ClipBuildStats {
  size_t raw_bytes = 0;
  size_t compressed_bytes = 0;
  uint32_t const_tracks = 0;   // t/r/s sabit iz sayisi (toplam 3*joints icinden)
  float max_rot_error_deg = 0; // niceleme sonrasi en buyuk donus hatasi
  float max_pos_error = 0;     // en buyuk ceviri hatasi (dunya birimi)
};

class ClipBuilder {
public:
  // Bake: raw -> blob (arenadan). Hata olcumu icin raw yeniden ornekle karsilastirilir.
  static const ClipHeader *build(Arena &arena, const RawClip &raw, float const_eps, ClipBuildStats *stats);
};

struct LocalPose {
  Vec3 *t;
  Quat *r;
  Vec3 *s;
  uint32_t joints;
};

class ClipSampler {
public:
  // t saniye; dongusel. Sifir ayirma. `out` cagiranin (joints kadar).
  static void sample(const ClipHeader *clip, float time_s, LocalPose &out);
  // Yerel poz -> model uzayi matrisleri (ebeveyn zinciri, ebeveyn indeksi < cocuk varsayilir).
  static void to_model(const Skeleton &sk, const LocalPose &local, Mat4 *out_model);
  // Skin matrisleri: model[j] * ters_bind[j] (GPU skinning girdisi).
  static void skin_matrices(const Mat4 *model, const Mat4 *inverse_bind, uint32_t joints, Mat4 *out_skin) {
    for (uint32_t j = 0; j < joints; j++) out_skin[j] = model[j] * inverse_bind[j];
  }
};

} // namespace tulpar::engine::sim
