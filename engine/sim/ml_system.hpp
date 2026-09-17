// L4 SIMULATION — Ogrenilmis (ML) fizik/davranis yaklasiklamasi.
//
// **ncnn YERINE core/math/nn.hpp -- BILINCLI ve GEREKCELI degisiklik:**
// bu dosya once Tencent ncnn'i (birkac MB'lik, SIMD yollu mobil cikarim
// motoru) kullaniyordu. Iki somut sorun vardi:
//   1) DETERMINIZM: ncnn calisma zamaninda CPU'ya gore farkli SIMD yolu
//      (AVX2/SSE/NEON) secer; ayni girdi farkli cihazda BIT-FARKLI cikti
//      verebilir. Motorun ana yapisal iddiasi "3 platform bit-es"
//      (VIZYON.md SS1) -- ogrenilmis fizik SIMULASYONU etkiliyorsa bu
//      iddia dogrudan CIGNENIR (rollback netcode aninda desenkron olur).
//   2) IKILI BOYUT: hedef ~12 MB; tek basina ncnn bu butcenin buyuk
//      kismini yer.
// core/math/nn.hpp (bu motorun kendi, test edilmis MLP cikarimi) ReLU/
// None yolunda YALNIZCA carpma-toplama kullanir -- libm YOK, SIMD dallanmasi
// YOK, dolayisiyla HER platformda bit-es. Ogrenilmis model artik
// sim/rollback.hpp'nin yeniden-simulasyonu ICINDE guvenle calisabilir --
// bu, buyuk motorlarin HICBIRINDE olmayan bir ozellik.
//
// ncnn hala third_party/'de duruyor: OYUN MANTIGI DISI (ör. arayuz, icerik
// uretimi, cevrimdisi arac) kullanimlar icin uygundur -- ama simulasyon
// yoluna girmemelidir.
//
// ECS'den BAGIMSIZ: duz diziler uzerinde calisir, hem sim::World hem flecs
// tarafindan surulebilir (ikisine de BAGLANMAZ).
#pragma once
#include <cstdint>

#include "core/math/nn.hpp"
#include "core/math/vec.hpp"

namespace tulpar::engine::sim {

// Ogrenilmis yaklasiklayici: agirliklar CAGIRAN tarafindan (cevrimdisi
// egitilip) verilir; bu sinif yalniz CIKARIMI yapar. Giris/cikis boyutlari
// agin KENDISINDEN okunur -- ayrica belirtmeye gerek yok, tutarsizlik olmaz.
struct MlApproximator {
  const Sequential *net = nullptr;

  bool valid() const { return net != nullptr && net->layer_count > 0; }
  uint32_t input_size() const { return valid() ? net->layers[0].in_dim : 0; }
  uint32_t output_size() const { return valid() ? net->layers[net->layer_count - 1].out_dim : 0; }
};

// Tek bir ornegi cikarir. scratch/scratch_capacity: core/math/nn.hpp'nin
// ping-pong arabellegi (cagiran ayirir; EN AZ 2 * en genis ARA katman).
// false: gecersiz yaklasiklayici, eksik tampon ya da cikarim basarisiz.
bool ml_infer(const MlApproximator &approx, const float *input, float *output, float *scratch,
              uint32_t scratch_capacity);

// Yaygin ozel durum: 3B konum/hiz cifti -> tahmini konum DELTASI (ör.
// kumas/yumusak-cisim dugumu icin agir cozucu yerine ogrenilmis tahmin).
// Giris duzeni: [pos.x,pos.y,pos.z, vel.x,vel.y,vel.z] (6 giris),
// cikti: [dx,dy,dz] (3 cikis) -- agin boyutlari BUNLARLA eslesmiyorsa
// false doner (sessizce yanlis yorumlama YOK).
bool ml_predict_delta(const MlApproximator &approx, Vec3 pos, Vec3 vel, Vec3 *out_delta, float *scratch,
                      uint32_t scratch_capacity);

} // namespace tulpar::engine::sim
