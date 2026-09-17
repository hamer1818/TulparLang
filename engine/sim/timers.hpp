// L4 SIMULATION — Zamanlayici: "N tick sonra su islevi cagir" (tek atislik
// ve periyodik), O(1) kurma/iptal.
//
// **SIFIRDAN YAZILMADI.** Cekirdek, jsnell/ratas'in hiyerarsik zamanlayici
// carkidir (github.com/jsnell/ratas, **MIT**, 220 yildiz, tek dosya, harici
// bagimlilik yok). third_party/ratas/timer-wheel.h altinda DEGISTIRILMEDEN
// duruyor. Bu dosya yalnizca onu Tulpar'in kurallarina baglar.
//
// **Neden bu kutuphane:** olculerek secildi, tahminle degil --
//   * `throw`/`catch` sayisi: 0            -> -fno-exceptions ile uyumlu
//   * `dynamic_cast`/`typeid` sayisi: 0    -> -fno-rtti ile uyumlu
//   * `new`/`malloc` sayisi: 0 (dosyadaki 4 eslesme YORUM icinde) -> carkin
//     tum depolamasi uye dizi; olaylar cagiran-sahipli, ic ice bagli liste
//     dugumleri. KURMA ve IPTAL TAHSISSIZ -> AllocGate kapisiyla uyumlu.
//   * **Saati DISARIDAN aliyor** (`advance(delta)`); kendi `clock_gettime`i
//     YOK. Tulpar icin BELIRLEYICI olan ozellik budur: cark bizim
//     `FixedStep::tick` sayacimizla surulur, yani duvar saatinden bagimsiz,
//     her platformda ayni tick'te ayni geri cagirma -> replay ve rollback
//     ile uyumlu. Kendi saatini okuyan bir zamanlayici bu motora GIREMEZDI.
//
// **Neden ad alaninda GORUNMUYOR:** ratas kendi ad alanini kullanmaz; global
// alana `Tick`, `TimerWheel`, `TimerEventInterface` isimlerini koyar. Bu
// yuzden kutuphane TAMAMEN timers.cpp'nin icine hapsedildi; bu baslik yalniz
// motorun kendi `Handle` tipini gorur. Bir gun cark degistirilse bu baslik
// DEGISMEZ.
//
// **Onemli anlamsal detaylar (hepsi test edilmistir):**
//   * Periyodik zamanlayici SURUKLENMEZ: yeniden kurma, geri cagirmanin
//     CAGRILDIGI tick'e goredir (cark icinde `now()` o tick'i dondurur),
//     "simdi"ye gore degil. 1000 tick sonra da faz AYNIDIR.
//   * Bir geri cagirma icinden zamanlayici kurmak/iptal etmek GUVENLIDIR.
//   * Periyodik zamanlayici, geri cagirma CALISMADAN ONCE yeniden kurulur;
//     boylece geri cagirma kendini `cancel()` ederse GERCEKTEN durur
//     (ters sirada kurulsaydi iptal edilmis zamanlayici dirilirdi).
#pragma once
#include <cstdint>

#include "core/memory/pool.hpp" // Handle (indeks + NESIL)

namespace tulpar::engine {
class Arena;
}

namespace tulpar::engine::sim {

// Geri cagirma: DUZ islev isaretcisi. `std::function` BILINCLI olarak
// kullanilmiyor -- yakalama (capture) heap tahsisi yapabilir ve motorun
// kare-basina-sifir-tahsis kuralini sessizce kirardi.
using TimerFn = void (*)(void *user);

struct TimerStats {
  uint64_t fired = 0;    // toplam calisan geri cagirma
  uint32_t active = 0;   // su an kurulu zamanlayici
  uint32_t rejected = 0; // havuz dolu ya da gecersiz sure -> KURULAMADI (sessiz degil)
};

class TimerSet {
public:
  // capacity: AYNI ANDA kurulu olabilecek en fazla zamanlayici. Tum bellek
  // burada bir kez alinir; sonrasinda tahsis YOKTUR.
  bool init(Arena &arena, uint32_t capacity);

  // `delay_ticks` tick SONRA bir kez calisir, sonra kendini serbest birakir.
  // delay_ticks == 0 REDDEDILIR (gecersiz handle doner, `rejected` artar):
  // carkin sozlesmesi delta != 0 ister; sessizce 1'e yuvarlamak cagiranin
  // hatasini gizlerdi.
  Handle after(uint32_t delay_ticks, TimerFn fn, void *user);

  // Her `period_ticks` tick'te bir calisir; IPTAL EDILENE kadar. Suruklenmez.
  Handle every(uint32_t period_ticks, TimerFn fn, void *user);

  // Kurulu zamanlayiciyi iptal eder ve slotu geri verir.
  // BAYAT handle guvenlidir: nesil etiketi tutmazsa `false` doner --
  // baskasinin zamanlayicisini iptal ETMEZ.
  bool cancel(Handle h);

  bool active(Handle h) const;
  // Kac tick kaldi. Kurulu degilse 0.
  uint32_t remaining(Handle h) const;

  // Sim'i `ticks` kadar ilerletir ve bu araliga dusen TUM geri cagirmalari
  // SIRAYLA calistirir (tick X'teki hepsi, tick X+1'dekilerden once).
  // ticks == 0 ise hicbir sey yapmaz.
  //
  // Butce SINIRI BILINCLI OLARAK KONULMADI: cark bir "en fazla N olay"
  // parametresi sunuyor, ama onu kullanmak ayni kare suresinde farkli
  // sayida geri cagirma calistirabilirdi -- bu, BELIRLENIMI bozar.
  // Bu motorda belirlenim > kare duzgunlugu.
  void advance(uint32_t ticks);

  // Carkin ic tick sayaci (advance cagrilari toplami).
  uint64_t now() const;

  // Bir sonraki zamanlayiciya kac tick kaldi; hicbiri yoksa `max`.
  // MOBIL ICIN ONEMLI: bos beklerken donmek yerine uyumaya izin verir.
  uint32_t ticks_to_next_event(uint32_t max) const;

  TimerStats stats() const;

  // `Impl` PUBLIC bildirilir (tanim .cpp'de kalir) — motorun her yerindeki
  // kalip bu (bkz. sim/physics.hpp): .cpp'deki ad alani duzeyi yardimcilari
  // `TimerSet::Impl*` alabilsin diye. `private:` altindayken timers.cpp
  // derlenmiyordu ("is private within this context").
  struct Impl;

private:
  Impl *impl_ = nullptr;
};

} // namespace tulpar::engine::sim
