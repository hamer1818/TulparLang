// L4 SIMULATION — SPH (Smoothed Particle Hydrodynamics) akiskan simulasyonu.
//
// **Kendi turetimimiz DEGIL:** cekirdek (kernel) fonksiyonlari ve kuvvet
// formulleri Muller, Charypar & Gross 2003, "Particle-Based Fluid Simulation
// for Interactive Applications" (SCA'03) makalesinden -- gercek-zamanli SPH
// alanininin KURUCU ve hala en cok uygulanan referansi:
//
//   Yogunluk (Poly6):      W(r,h)    = 315/(64*pi*h^9) * (h^2-r^2)^3
//   Basinc (Spiky grad):   grad W    = -45/(pi*h^6) * (h-r)^2 * r_hat
//   Viskozite (Laplacian): lap W     =  45/(pi*h^6) * (h-r)
//
// Farkli cekirdekler KASITLI (makalenin ana katkisi): Poly6 ucuzdur ama
// gradyani r->0'da sifira gider (parcaciklar kumelenir), bu yuzden BASINC
// icin Spiky kullanilir; viskozite icin de Laplacian'i NEGATIF olmayan bir
// cekirdek gerekir.
//
// **third_party/SPlisHSPlasH ile iliski:** o kutuphane arastirma seviyesinde
// (DFSPH/IISPH, cok daha dogru ve pahali) ve su an CMakeLists'e BAGLI DEGIL.
// Burasi mobil butceye uygun, bagimliliksiz, TAM implemente edilmis temel
// SPH -- ayni sahnede Jolt kati cisimleriyle birlikte calistirilabilir
// (sinir kuvvetleri cagiranin isi).
//
// Komsu arama: O(n^2) kaba kuvvet. BILINCLI ilk dilim -- dogruluk once;
// uzamsal hash/izgara sonraki optimizasyon (parcacik sayisi mobilde zaten
// birkac bin ile sinirli). Bu, sonucu DEGISTIRMEZ, yalnizca hizlandirir.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::sim {

struct SphParticle {
  Vec3 pos{0, 0, 0};
  Vec3 vel{0, 0, 0};
  float density = 0.0f;  // her adimda YENIDEN hesaplanir
  float pressure = 0.0f; // density'den turetilir
};

struct SphParams {
  float smoothing_radius = 0.2f; // h -- bu mesafenin otesindeki komsu KATKI VERMEZ
  float particle_mass = 0.02f;
  float rest_density = 1000.0f;  // su: 1000 kg/m^3
  float stiffness = 3.0f;        // k (durum denklemi: p = k*(rho - rho0))
  float viscosity = 0.1f;        // mu
  Vec3 gravity{0.0f, -9.81f, 0.0f};
};

// --- Cekirdek fonksiyonlari (test edilebilmeleri icin ACIK) ------------
float sph_poly6(float r, float h);            // yogunluk cekirdegi
float sph_spiky_grad_magnitude(float r, float h); // basinc gradyaninin BUYUKLUGU (isaretsiz)
float sph_viscosity_laplacian(float r, float h);

// Yogunluk + basinci HESAPLAR (parcaciklarin density/pressure alanlarini yazar).
void sph_compute_density(SphParticle *particles, uint32_t count, const SphParams &params);

// Basinc+viskozite+yercekimi kuvvetlerini uygular ve yari-kapali Euler ile
// ilerletir. sph_compute_density() ONCE cagrilmis olmalidir.
//
// accel_scratch: cagiranin ayirdigi count elemanlik dizi -- ZORUNLU, cunku
// adim IKI FAZLIDIR: (1) TUM ivmeler, HICBIR parcacik guncellenmeden once
// hesaplanir, (2) sonra hepsi birden ilerletilir. Tek fazda yapilsaydi
// sonraki parcaciklar oncekilerin ZATEN GUNCELLENMIS konumunu gorurdu
// (Gauss-Seidel) -- sonuc parcacik SIRASINA bagli hale gelir, fiziksel
// olarak yanlis olurdu. Fonksiyon donunce dizinin icerigi her parcacigin
// o adimdaki ivmesidir (hata ayiklama/gorsellestirme icin okunabilir).
void sph_step(SphParticle *particles, uint32_t count, const SphParams &params, float dt,
              Vec3 *accel_scratch);

} // namespace tulpar::engine::sim
