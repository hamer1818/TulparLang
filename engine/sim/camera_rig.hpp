// L4 SIMULATION — Kamera sistemi temel taslari (500 madde listesi #168-177
// "Kamera sistemleri"; DEVAM_PLANI.md Faz B madde 7'nin karsiligi: "takip,
// carpisma-farkindalik, sarsinti, FOV gecisi... cogu projenin GERCEKTE
// takildigi yer"). Bilincli dar kapsam: YALNIZ takip (smooth follow) +
// sarsinti (shake) + FOV/deger yumusatma. Carpisma-farkindalik (kamera-
// duvar raycast) navmesh/fizik entegrasyonu gerektirir, ayri bir bahis --
// burasi o entegrasyonun UZERINE oturacagi temel.
#pragma once
#include "core/math/vec.hpp"

namespace tulpar::engine::sim {

// Kare-hizindan BAGIMSIZ ussel yumusatma (Freya Holmer'in "lerp is broken"
// ilkesi -- sabit bir katsayiyla her kare `current += (target-current)*t`
// yapmak degisken dt'de FARKLI yakinsama hizi verir; bu, dt'den BAGIMSIZ
// dogru sonucu verir). half_life_s: mesafenin TAM YARIYA inmesi icin gecen
// sure -- iki yari-omur sonra CEYREGINE, ucu sonra sekizde-birine iner (bu
// ozellik testlerde ELLE dogrulanir, exp() hesaplamaya gerek kalmadan).
float exponential_smooth(float current, float target, float half_life_s, float dt_s);
Vec3 exponential_smooth(Vec3 current, Vec3 target, float half_life_s, float dt_s);

// Squirrel Eiserloh'un "trauma" modelinin BASITLESTIRILMIS surumu (GDC 2016
// "Juice It or Lose It"): Perlin gurultusu yerine sabit-frekansli sinus
// toplami kullanir -- deterministik, elle sinir-kanitlanabilir (Perlin'in
// aksine tam deger hesaplamaya gerek yok, |sin|<=1 kimliginden max_offset
// siniri HER ZAMAN tam saglanir). trauma^2 ile olceklenir (kucuk sarsintida
// yumusak baslama/bitis, Eiserloh'un onerdigi gibi).
struct CameraShake {
  float trauma = 0.0f;
  float decay_per_s = 1.0f; // trauma bu hizda (birim/saniye, DOGRUSAL) 0'a doner

  // amount kadar trauma ekler, [0,1] araligina KIRPAR (ust siniri asamaz).
  void add_trauma(float amount);
  // dt_s kadar zamani ilerletir (trauma soner), time_s anindaki ofseti
  // (konum, dunya birimi) doner. time_s: cagiranin izledigi TOPLAM sure
  // (sinus fazlari icin -- MONOTON ARTAN olmasi beklenir, sim zamanindan).
  Vec3 update(float dt_s, double time_s, float max_offset = 0.3f);
};

// FOV/Zoom gecisi (500 madde listesi #174/#175 "Camera Zoom"/"Camera FOV"):
// HER IKISI de exponential_smooth'un DOGRUDAN bir kullanimidir (tek skaler
// deger tasidiklari icin ayri bir formul GEREKMEZ) -- burada yalniz
// ISIMLENDIRILMIS, dar kapsamli birer sarmalayici, cagiran kodun niyetini
// (hangi degerin nereye gittigini) netlestirir.
struct FovController {
  float fov_deg = 60.0f;
  float half_life_s = 0.2f;
  float update(float target_fov_deg, float dt_s) {
    fov_deg = exponential_smooth(fov_deg, target_fov_deg, half_life_s, dt_s);
    return fov_deg;
  }
};

struct ZoomController {
  float distance = 5.0f; // hedeften kameraya mesafe carpani (FollowCamera.offset ile carpilir)
  float half_life_s = 0.2f;
  float update(float target_distance, float dt_s) {
    distance = exponential_smooth(distance, target_distance, half_life_s, dt_s);
    return distance;
  }
};

// Basit "arkadan takip" kamerasi: hedef+ofset noktasina ussel yumusatmayla
// yaklasir, Mat4::look_at ile gorus matrisini uretir.
struct FollowCamera {
  Vec3 position{0, 0, 0};
  float position_half_life_s = 0.15f;

  // target: takip edilen nokta. offset: hedeften kameraya DUNYA-uzayi ofset
  // (ör. {0,2,-5} = arkadan-yukaridan). position UYESINI GUNCELLER (yan etki).
  Mat4 update(Vec3 target, Vec3 offset, Vec3 up, float dt_s);
};

} // namespace tulpar::engine::sim
