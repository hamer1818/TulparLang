// L4 SIMULATION — Rollback netcode cekirdegi (500 madde listesi #5/#431:
// "Rollback hazirligi", "Rollback Netcode" — bkz. docs/engine/VIZYON.md SS1,
// docs/engine/MADDE-LISTESI-DURUM.md). World::snapshot()/restore() (ecs.hpp)
// UZERINE kurulu: World'un TUM durumu init()'te ayrilan SABIT-ADRES arena
// bloklarinda yasadigi icin anlik goruntu tam bir bayt-kopyasidir (ecs.hpp'deki
// "Rollback hazirligi" yorumuna bkz) — bu sinif o temelin standart GGPO/GGRS
// tarzi (input buffer + save/restore/resimulate) desenidir. Tulpar'in
// determinizm avantaji (VIZYON.md SS1: 3 platform bit-es) burada ZATEN
// varsayilan: ayni girdi dizisiyle resimulasyon HER ZAMAN ayni sonucu verir,
// bu yuzden "senkronizasyon dogrulama" ayri bir sorun degil (bkz. sim/desync.hpp).
//
// Referans: GGRS (gschup/ggrs, bu oturumda dogrulandi — MADDE-LISTESI-DURUM.md
// G.342) GGPO'nun ayni deseninin Rust'taki karsiligi; burada C++/Arena
// idiyomuna uyarlandi (kare icinde 0 allocation — VIZYON.md SS1 ilkesi).
//
// Zaman cizelgesi sozlesmesi: current_frame() = "sirada bekleyen, henuz
// save() edilmemis frame". save() cagrildiginda O ANKI world durumu (frame'in
// SIMULE EDILMEDEN ONCEKI hali) + o frame'i surecek girdiler kaydedilir, SONRA
// current_frame_ bir artar. Cagiran save() SONRASI world'u kendi step
// fonksiyonuyla ilerletir. apply_remote_input() SADECE zaten save() edilmis
// (< current_frame()) frame'ler icin gecerlidir — henuz simule edilmemis
// "su anki" kare icin dogru girdi zaten dogrudan predicted_remote olarak
// save()'e verilmelidir (tahmine gerek yok).
//
// Bellek: history_frames * (world_snapshot_bytes + 2*sizeof(Input) + 1) —
// TAMAMI init()'te Arena'dan (A2: kare icinde ayirma yok).
#pragma once
#include <cstdint>
#include <cstring>

#include "core/memory/arena.hpp"
#include "sim/ecs.hpp"

namespace tulpar::engine::sim {

// Input: POD, trivially-copyable, memcmp ile karsilastirilabilir olmali
// (oyun mantigi Tulpar tarafinda tanimlanir — DEVAM_PLANI.md'nin bilincli
// disarida biraktigi "dil baglamasi" bu sinifin ICINE sizmaz, T bir sablon
// parametresi olarak kalir).
template <class Input>
class RollbackBuffer {
 public:
  // world_snapshot_bytes: cagiranin World::snapshot_bytes()'ten aldigi sabit
  // deger. history_frames: GGPO tipik 7-8 (input gecikmesi/jitter'a gore
  // ayarlanir); 0 kabul edilmez.
  bool init(Arena &arena, uint64_t world_snapshot_bytes, uint32_t history_frames) {
    if (history_frames == 0 || world_snapshot_bytes == 0) return false;
    snapshot_bytes_ = world_snapshot_bytes;
    capacity_ = history_frames;
    snapshots_ = static_cast<uint8_t *>(arena.alloc(snapshot_bytes_ * capacity_, 16));
    local_inputs_ = arena.alloc_array<Input>(capacity_);
    remote_inputs_ = arena.alloc_array<Input>(capacity_);
    confirmed_ = arena.alloc_array_zeroed<bool>(capacity_);
    if (!snapshots_ || !local_inputs_ || !remote_inputs_ || !confirmed_) return false;
    current_frame_ = 0;
    oldest_frame_ = 0;
    resim_pending_ = false;
    resim_from_ = 0;
    return true;
  }

  uint32_t history_frames() const { return capacity_; }
  uint64_t current_frame() const { return current_frame_; }
  uint64_t oldest_frame() const { return oldest_frame_; }
  // [oldest_frame(), current_frame()) araligindaki frame'ler hala tamponda —
  // apply_remote_input/local_input_at/remote_input_at bunun disinda gecersiz.
  bool in_window(uint64_t frame) const { return frame >= oldest_frame_ && frame < current_frame_; }

  // Cagiran SIRAYLA, kare basina TAM BIR KEZ cagirir — world.step()'ten HEMEN
  // ONCE. predicted_remote: uzak oyuncunun bu kare icin TAHMIN edilen girdisi
  // (henuz gercek girdi gelmediyse son bilinen / bos girdi).
  void save(const World &world, const Input &local_input, const Input &predicted_remote) {
    const uint32_t s = slot(current_frame_);
    world.snapshot(snapshots_ + static_cast<uint64_t>(s) * snapshot_bytes_);
    local_inputs_[s] = local_input;
    remote_inputs_[s] = predicted_remote;
    confirmed_[s] = false;
    current_frame_++;
    oldest_frame_ = (current_frame_ > capacity_) ? (current_frame_ - capacity_) : 0;
  }

  // Agdan gelen GERCEK uzak girdi. Donus true: tahmin YANLIS cikti VE
  // resimulasyon gerekiyor (needs_resimulation()/resimulate() ile). frame
  // pencere disindaysa (cok gec geldi -- history_frames asildi) SESSIZCE
  // false doner; cagiran in_window() ile onceden kontrol edip bu durumu
  // (kalici desync riski) sim/desync.hpp ile ayrica ele almali.
  bool apply_remote_input(uint64_t frame, const Input &actual) {
    if (!in_window(frame)) return false;
    const uint32_t s = slot(frame);
    const bool changed = std::memcmp(&remote_inputs_[s], &actual, sizeof(Input)) != 0;
    remote_inputs_[s] = actual;
    confirmed_[s] = true;
    if (changed) {
      if (!resim_pending_ || frame < resim_from_) resim_from_ = frame;
      resim_pending_ = true;
    }
    return changed;
  }

  bool needs_resimulation() const { return resim_pending_; }
  uint64_t resimulate_from() const { return resim_from_; }
  bool is_confirmed(uint64_t frame) const { return in_window(frame) && confirmed_[slot(frame)]; }

  // TEK GIRIS NOKTASI: en eski yanlis-tahmin edilen kareye geri doner, o
  // kareden current_frame()'e kadar step(world, local_input, remote_input)'u
  // TEKRAR cagirarak yeniden oynatir (artik DUZELTILMIS girdilerle), ve her
  // adimda bir sonraki karenin anlik goruntusunu GUNCELLER (boylece daha
  // SONRA baska bir duzeltme gelirse dogru pre-state'ten devam eder). Resim
  // bekleyen yoksa hicbir sey yapmaz (ucuz no-op).
  template <class StepFn>
  void resimulate(World &world, StepFn &&step) {
    if (!resim_pending_) return;
    world.restore(snapshots_ + static_cast<uint64_t>(slot(resim_from_)) * snapshot_bytes_);
    for (uint64_t f = resim_from_; f < current_frame_; f++) {
      const uint32_t s = slot(f);
      step(world, local_inputs_[s], remote_inputs_[s]);
      const uint64_t next = f + 1;
      if (next < current_frame_) {
        const uint32_t ns = slot(next);
        world.snapshot(snapshots_ + static_cast<uint64_t>(ns) * snapshot_bytes_);
      }
    }
    resim_pending_ = false;
  }

 private:
  uint32_t slot(uint64_t frame) const { return static_cast<uint32_t>(frame % capacity_); }

  uint8_t *snapshots_ = nullptr;
  Input *local_inputs_ = nullptr;
  Input *remote_inputs_ = nullptr;
  bool *confirmed_ = nullptr;
  uint64_t snapshot_bytes_ = 0;
  uint32_t capacity_ = 0;
  uint64_t current_frame_ = 0;
  uint64_t oldest_frame_ = 0;
  bool resim_pending_ = false;
  uint64_t resim_from_ = 0;
};

} // namespace tulpar::engine::sim
