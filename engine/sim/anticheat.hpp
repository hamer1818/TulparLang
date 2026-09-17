// L4 SIMULATION — En ucuz hareket-makuliyeti kontrolu (420 madde listesi
// #14: "Anti-Cheat"). Tam bir anti-cheat sistemi (bellek butunlugu, kod
// imzalama, sunucu-yetkili yeniden hesaplama) KAPSAM DISI -- bu yalnizca
// EN TEMEL kontrolu yapar: iki ardisik durum arasindaki ima edilen hiz
// fiziksel olarak imkansizsa (isinlanma/teleport hilesi) isaretler.
//
// ONEMLI SINIR: sunucu-yetkili mimaride (rollback netcode ile birlikte,
// VIZYON.md S2.3) bu kontrol GUVENILIR TARAFTA (sunucu/host) calistirilmali
// -- yalniz istemci tarafinda calistirmak erken-uyari/UX'tir, guvenlik
// SINIRI degildir (istemcinin kendi kodu istemcide her zaman guvenilmez).
#pragma once
#include "core/math/vec.hpp"

namespace tulpar::engine::sim {

struct SpeedCheckResult {
  bool suspicious = false;
  float implied_speed = 0; // birim/saniye
};

// max_speed: oyunun bilinen en hizli mesru hareketi + guvenlik payi (birim/s).
// dt<=0 (yinelenen paket/ayni tick) => suspicious=false, implied_speed=0 --
// eksik/celiskili zaman verisi hile KANITI degildir, bolme-sifir da olmaz.
SpeedCheckResult speed_check(Vec3 prev_pos, Vec3 new_pos, float dt, float max_speed);

} // namespace tulpar::engine::sim
