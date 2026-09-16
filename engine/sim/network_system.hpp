// L4 SIMULATION — Rollback (geriye sarma) netcode surucusu.
//
// **Bu dosya YENI bir rollback UYGULAMASI DEGIL:** motorda ZATEN tam
// implemente edilmis ve test edilmis bir cekirdek var -- sim/rollback.hpp
// (GGPO/GGRS deseni: girdi tamponu + save/restore/resimulate). Burasi o
// cekirdegi GERCEK bir oyun dongusune baglayan ince surucu katmanidir:
// kim ne zaman save() eder, uzak girdi gelince kim resimulate() cagirir.
//
// **Onceki durum ve neden degisti:** bu dosya daha once bos bir flecs
// sistemiydi ("1. Snapshot yukle, 2. Eksik inputlari simule et" yorumlari,
// kod yok) ve rollback'i SIFIRDAN yazmaya hazirlaniyordu. Bu hem ikinci bir
// uygulama demek (iki farkli davranis, iki farkli hata kaynagi) hem de
// gereksiz: sim/rollback.hpp'nin bagli oldugu tek eksik parca --
// World::snapshot()/restore() -- ARTIK sim/ecs.hpp'de mevcut, yani cekirdek
// KULLANILABILIR durumda.
//
// ECS baglamasi: dogrudan sim::World uzerinde calisir (rollback zaten
// World'un snapshot/restore'una dayanir). flecs tarafindan surulmek
// isteniyorsa, flecs sistemi yalnizca asagidaki fonksiyonlari CAGIRIR.
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "sim/ecs.hpp"
#include "sim/rollback.hpp"

namespace tulpar::engine::sim {

// Input: POD, trivially-copyable (rollback.hpp ile AYNI kisit).
template <class Input>
class RollbackSession {
 public:
  // history_frames: geriye ne kadar sarilabilecegi (tipik 8-16 kare; ag
  // gecikmesi / kare suresi kadar olmali).
  bool init(Arena &arena, const World &world, uint32_t history_frames) {
    return buffer_.init(arena, world.snapshot_bytes(), history_frames);
  }

  // Her SIMULASYON KARESINDE, adimdan HEMEN ONCE cagrilir: o karenin
  // durumunu ve girdilerini kaydeder. predicted_remote: uzak oyuncunun
  // girdisi HENUZ gelmediyse TAHMIN (yaygin ve dogru varsayilan: bir
  // onceki kareyle AYNI -- oyuncular tuslari basili tutar).
  void begin_frame(const World &world, const Input &local, const Input &predicted_remote) {
    buffer_.save(world, local, predicted_remote);
  }

  // Ag'dan gercek uzak girdi geldiginde cagrilir. true donerse TAHMIN
  // YANLISTI -- o kareye geri donup yeniden simule etmek GEREKIR.
  bool on_remote_input(uint64_t frame, const Input &actual) {
    return buffer_.apply_remote_input(frame, actual);
  }

  // on_remote_input() true dondurduyse cagrilir: dunyayi ilgili kareye geri
  // yukler ve araya giren TUM kareleri DOGRU girdilerle yeniden oynatir.
  // step: void(World&, const Input& local, const Input& remote, uint64_t frame)
  // imzasinda, oyunun TEK bir simulasyon adimini calistiran fonksiyon.
  template <class StepFn>
  void resolve(World &world, StepFn &&step) {
    buffer_.resimulate(world, static_cast<StepFn &&>(step));
  }

  const RollbackBuffer<Input> &buffer() const { return buffer_; }

 private:
  RollbackBuffer<Input> buffer_;
};

} // namespace tulpar::engine::sim
