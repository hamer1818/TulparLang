// L4 SIMULATION — Karakter/kalabalik hareketi icin iki KLASIK, tam
// implemente edilmis algoritma: FABRIK ters kinematigi (cok kemikli) ve
// akis alani (flow field) yol bulma.
//
// **Neden ucuncu-parti sarmalayici DEGIL:** third_party/fabrik gercek bir
// kutuphane, ama kendi sablon tip sistemiyle geliyor (vec_t<float,3>,
// strided_t<>, data_v1/math3d_v1) -- Tulpar'in Vec3'unu ona baglamak, hicbir
// seyi derleyip dogrulayamadigimiz bu ortamda gorunmez tip/ABI hatasi
// riski demek. FABRIK'in KENDISI ~40 satirlik, tamamen belgelenmis bir
// algoritma (Aristidou & Lasenby 2011, "FABRIK: A fast, iterative solver
// for the Inverse Kinematics problem") -- dogrudan kendi tiplerimizle
// yazmak HEM daha az bagimlilik HEM elle iz surulerek dogrulanabilir.
// Ayni gerekce content/voxel.hpp'nin greedy meshleme portunda da gecerliydi.
//
// sim/ik.hpp ile iliski: orada ANALITIK iki-kemik cozumu var (kosinus
// teoremi, TEK adimda kesin sonuc, omuz-dirsek-bilek gibi). Burasi N
// kemikli, YINELEMELI genel cozum (omurga, kuyruk, tentakul, kablo).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::sim {

constexpr uint32_t kFabrikMaxJoints = 32;

struct FabrikResult {
  uint32_t iterations = 0; // gercekte kac yineleme dondu (<= max_iterations)
  bool reached = false;    // uc eklem hedefe tolerans icinde ulasti mi
};

// positions[0..count): eklem zinciri, positions[0] KOK (SABIT kalir, hedef
// nereye giderse gitsin oynamaz). YERINDE guncellenir.
// lengths[0..count-1): kemik uzunluklari -- lengths[i] = |p[i+1]-p[i]| olmali
// (cagiran bind-pose'dan BIR KEZ hesaplar, her karede yeniden olcmez; boylece
// zincir gerildiginde kemikler UZAMAZ).
// Ulasilamayan hedef (|target-kok| > toplam uzunluk): zincir hedefe DOGRU
// tamamen DUZLESIR (FABRIK'in kendi tanimli davranisi), reached=false doner.
FabrikResult solve_fabrik(Vec3 *positions, const float *lengths, uint32_t count, Vec3 target,
                          float tolerance = 0.01f, uint32_t max_iterations = 10);

// --- Akis alani (flow field) yol bulma ---------------------------------
// Klasik RTS tekniği (Supreme Commander 2'nin populerlestirdigi): TEK bir
// hedef icin BIR KEZ tum izgaraya "maliyet" yayilir, sonra HER ajan yalniz
// kendi hucresindeki hazir yon vektorunu okur -- N ajan icin N ayri A*
// CALISTIRMAK yerine 1 yayilim + N dizi okumasi (kalabalik/suru hareketi
// icin dogru olcekleme).
//
// Iki asama: (1) integrasyon alani -- hedeften geriye dogru BFS ile her
// hucrenin hedefe uzakligi, (2) akis alani -- her hucreden EN DUSUK maliyetli
// komsuya birim vektor.
constexpr uint16_t kFlowFieldBlocked = 0xFFFF; // gecilemez hucre maliyeti

struct FlowField {
  uint32_t width = 0, height = 0;
  float cell_size = 1.0f;
  Vec3 origin{0, 0, 0}; // izgara (0,0) hucresinin DUNYA kosesi (xz duzlemi)

  // Asagidaki uc dizi CAGIRAN tarafindan width*height boyutunda ayrilir
  // (A2: bu dosya ayirma YAPMAZ).
  const uint8_t *blocked = nullptr; // 0 = gecilebilir, !=0 = duvar (giris)
  uint16_t *cost = nullptr;         // cikis: hedefe adim sayisi (integrasyon alani)
  Vec3 *flow = nullptr;             // cikis: her hucrede izlenecek birim yon (xz)
  uint32_t *queue = nullptr;        // BFS calisma alani (icerigi cagiran icin ANLAMSIZ)

  uint32_t index(uint32_t x, uint32_t z) const { return z * width + x; }
};

// goal_x/goal_z: hedef hucre koordinati. cost[] ve flow[] doldurulur.
// false: hedef izgara disinda ya da hedef hucre gecilemez.
bool build_flow_field(FlowField &field, uint32_t goal_x, uint32_t goal_z);

// Dunya konumundan o hucrenin akis yonunu okur. Izgara disi / ulasilamaz
// hucre -> {0,0,0} (cagiran "yon yok" olarak yorumlar).
Vec3 sample_flow(const FlowField &field, Vec3 world_pos);

} // namespace tulpar::engine::sim
