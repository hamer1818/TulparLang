// L4 SIMULATION — Lag compensation: sunucu tarafi ISABET testi icin geriye
// sarma (500 madde listesi #12/#432 "Lag Compensation" — sim/interp.hpp'nin
// #433 "Snapshot Interpolation"iyle ESLESEN ama FARKLI yarisi: interp.hpp
// GORUNTUYU/uzak varliklarin GORUNDUGU yeri yumusatir; bu dosya SUNUCUDAKI
// VURUS KARARINI adalet icin geriye sarar).
//
// **DOGRULANDI, VARSAYILMADI:** teknik Valve'in kendi yayinladigi, sektorde
// FIILEN standart referans olan Yahn Bernier'in 2001 makalesiyle ("Latency
// Compensating Methods in Client/Server In-game Protocol Design and
// Optimization", developer.valvesoftware.com/wiki/Lag_compensation) AYNI:
// DUSUK gecikmeli atici, KENDI ekraninda hedefi X anindaki konumunda gorup
// ates eder; sunucu bu isabeti hedefin O ANKI GUNCEL degil, atici gordugu
// ZAMANDAKI (render_time_s = simdi - atici_gecikmesi) konumuyla test
// etmezse, hizli hareket eden hedefler agdaki gecikme yuzunden GORUNURDE
// isabetli atislari kacirir.
//
// max_rewind_s: istismar onlemi ("peeker's advantage"i sinirlar) — cok eski
// bir render_time_s'e sarma sessizce REDDEDILIR (rewind()/raycast() false
// doner). Varsayilan deger (1.0 saniye) TESADUF DEGIL: Source motorunun
// GERCEK `sv_maxunlag` degiskeninin (developer.valvesoftware.com/wiki/
// Lag_compensation) varsayilan degeri VE ayni [0,1] araligi BIREBIR bu.
//
// Bilincli olarak DAR kapsam (interp.hpp'nin "yalniz pozisyon+yaw tasir"
// kaydiyla ayni ruh): yalniz KURE (merkez+yaricap) hitbox, silah/hasar/
// oyun mantigi TASIMAZ — core/math/vec.hpp'nin Ray/Sphere kesisimiyle yalniz
// "bu isin, bu zamanda, hangi izlenen hedefe carpti" sorusuna cevap verir.
//
// Ayirma yok (A2): kMaxTracks*kHistoryPerTrack sabit boyutlu, sinif icinde.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::sim {

struct HitboxSample {
  double time_s = 0; // MONOTON ARTAN sirayla record() edilmeli (interp.hpp ile ayni kural)
  Vec3 center{0, 0, 0};
  float radius = 0.0f;
};

class LagCompensator {
 public:
  static constexpr uint32_t kMaxTracks = 64;       // ayni anda izlenen hedef sayisi
  static constexpr uint32_t kHistoryPerTrack = 32; // hedef basina saklanan ornek

  explicit LagCompensator(double max_rewind_s = 1.0) : max_rewind_s_(max_rewind_s) {}

  // track: cagiranin atadigi kimlik (genelde entity index'i), [0,kMaxTracks).
  void record(uint32_t track, double time_s, Vec3 center, float radius);

  // O track'in render_time_s anindaki DOGRUSAL ENTERPOLE hitbox'i. Sinirlarin
  // disinda KILITLENIR (InterpBuffer ile ayni sozlesme: ekstrapolasyon yok).
  // now_s - render_time_s > max_rewind_s_ ISE (asiri eski istek) VEYA gecmis
  // bos/yetersizse false doner.
  bool rewind(uint32_t track, double now_s, double render_time_s, HitboxSample *out) const;

  // TUM aktif track'leri render_time_s anina sarar, ray'i her biriyle test
  // eder, EN YAKIN (en kucuk t) isabeti doner. Vurus yoksa false.
  bool raycast(const Ray &ray, double now_s, double render_time_s, uint32_t *hit_track,
               float *hit_t) const;

  void clear_track(uint32_t track);
  double max_rewind_s() const { return max_rewind_s_; }

 private:
  struct Track {
    HitboxSample buf[kHistoryPerTrack] = {};
    uint32_t count = 0; // dolu yuva sayisi (<= kHistoryPerTrack)
    uint32_t next = 0;  // bir sonraki record()'un halka indeksi (mod alinmadan, monoton artar)
    bool active = false;
  };
  Track tracks_[kMaxTracks];
  double max_rewind_s_;
};

} // namespace tulpar::engine::sim
