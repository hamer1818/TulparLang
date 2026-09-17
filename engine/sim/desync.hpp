// L4 SIMULATION — Desync/rollback yapi tasi (420 madde listesi #5/#391/#399:
// "Rollback hazirligi", "Rollback Netcode", "Determinism Desync Detector").
// World::content_hash() ZATEN VAR (sim/ecs.hpp, Faz 2 replay kapisi) — bu
// dosya onu AG-BAGIMSIZ (network-agnostic) bir tarihce + karsilastirma
// sarmalayicisina cevirir. Gercek P2P/rollback netcode (soket, girdi
// senkronizasyonu, resimulasyon) KAPSAM DISI; bu yalniz "iki tarafin durumu
// hala ayni mi" sorusuna cevap veren PRIMITIVE'i saglar — DEVAM_PLANI.md'de
// gecen "rollback neredeyse bedava" iddiasinin somut ilk parcasi.
//
// Kullanim: her tick sonunda `history.push(tick, world.content_hash())`;
// uzaktan bir tick+hash geldiginde `history.compare(tick, remote_hash)`.
#pragma once
#include <cstdint>

namespace tulpar::engine::sim {

class DesyncHistory {
public:
  static constexpr uint32_t kMaxHistory = 256;

  enum class Result { kMatch, kMismatch, kUnknown };

  // Ayirma yok (A2): sabit halka, kMaxHistory asilinca en eski kayit
  // sessizce UZERINE YAZILIR.
  void push(uint32_t tick, uint64_t hash);
  // tick tarihcede yoksa (cok eski/hic kaydedilmemis) false doner.
  bool get(uint32_t tick, uint64_t *out_hash) const;
  // Uzak tarafin (agdan gelen) tick+hash'ini kendi kaydimizla karsilastirir.
  Result compare(uint32_t tick, uint64_t remote_hash) const;

private:
  uint32_t ticks_[kMaxHistory] = {};
  uint64_t hashes_[kMaxHistory] = {};
  bool valid_[kMaxHistory] = {};
  uint32_t next_ = 0;  // bir sonraki push'un halka indeksi (mod alinmadan, monoton artar)
  uint32_t count_ = 0; // su ana kadar dolu slot sayisi (<= kMaxHistory)
};

} // namespace tulpar::engine::sim
