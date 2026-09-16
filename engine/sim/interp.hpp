// L4 SIMULATION — Anlik-goruntu (snapshot) enterpolasyonu (420 madde listesi
// #393 "Snapshot Interpolation", #392 "Lag Compensation"in render tarafi).
// Agdan (ya da sim'den) SEYREK gelen durum orneklerini (her network/sim tick)
// render karesi icin PURUZSUZ hale getirir -- MAME4droid/GGRS tarzi rollback
// yerine, gelen VERININ kendisini yumusatan tamamlayici teknik (VIZYON.md
// S2.3'teki ekosistem notuyla ayni aile).
//
// Bilincli olarak KUCUK kapsam: yalniz pozisyon (Vec3) + duzlemsel donus
// (derece, en kisa yol enterpolasyonu) tasir. Tam Transform/quaternion
// enterpolasyonu (core/math/vec.hpp Transform/Quat zaten var) ayri, daha
// genel bir is -- ilk dilim burada kasitli dar tutuldu.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::sim {

struct InterpSample {
  double time_s = 0; // ornegin ait oldugu SIM zamani (saniye, MONOTON ARTAN sirayla push edilmeli)
  Vec3 pos{0, 0, 0};
  float yaw_deg = 0;
};

// Sabit boyutlu halka: son kMaxHistory ornek. Ayirma yok (A2).
class InterpBuffer {
public:
  static constexpr uint32_t kCapacity = 16;

  void push(const InterpSample &s);
  // render_time_s: cagiranin sectigi GERI TARIHLI render ani (agdan gelen
  // dogal gecikmeyi gizlemek icin genelde "simdi - interp_delay_s").
  // Sinirlarin disinda KILITLENIR (dis-ari tahmin/ekstrapolasyon yapilmaz --
  // en eski/en yeni ornege sabitlenir, bu daha kararlidir). 2'den az ornek
  // varsa false doner (cagiran son bilinen durumu kendisi tutmali).
  bool sample(double render_time_s, InterpSample *out) const;
  uint32_t count() const { return count_; }

private:
  InterpSample buf_[kCapacity] = {};
  uint32_t count_ = 0; // dolu yuva sayisi (<= kCapacity)
  uint32_t next_ = 0;  // bir sonraki push'un halka indeksi (mod alinmadan, monoton artar)
};

} // namespace tulpar::engine::sim
