// L1 CORE — Kucuk ileri-beslemeli sinir agi cikarimi (500 madde listesi
// #15 "Noral Rendering"in RUHU, ama Tulpar'in GERCEK hedef donanimina
// UYGUN sekilde: NPU/neural-accelerator GEREKTIRMEZ).
//
// NEDEN NPU DEGIL: Mali G2-Ultra NX gibi neural-accelerator'lar (VIZYON.md
// SS3'te web'den dogrulandi -- gercek, 8 Eylul 2026 duyuruldu) SADECE en
// yeni AMIRAL GEMISI cihazlarda var. Tulpar'in butun stratejisi "tum cihaz
// yelpazesi" (orta/dusuk segment DAHIL) -- NPU'ya sabit bagimlilik bu
// stratejiyle DOGRUDAN CELISIR (VIZYON.md'nin defalarca vurguladigi hata:
// "amiral gemisi cihazlar icin optimize, geri kalani unut").
//
// **BURADA DEVRIMSEL OLAN SEY:** ReLU aktivasyonuyla bu sinir agi TAMAMEN
// DETERMINISTIKTIR -- toplama/carpma/max disinda HICBIR islem yok, libm
// (exp/tanh gibi transandantal fonksiyonlar PLATFORMLAR ARASI son-bitte
// FARKLI yuvarlayabilir) KULLANILMAZ. Bu, motorun asil ayirt edici
// ozelligiyle (VIZYON.md SS1: "3 platform bit-es determinizm") AYNI
// cizgide: KUCUK bir sinir agi, rollback netcode'un (sim/rollback.hpp)
// ayni deterministik simulasyon ADIMI icinde, TUM istemcilerde BIT-ES
// SONUC vererek calisabilir -- ornegin ogrenilmis bir NPC davranis
// agi/animasyon karistirici. Unity/Unreal/Godot'un "noral" ozellikleri ya
// editor-zamanli araclardir ya da NPU/GPU'da calisir ve DETERMINISTIK
// DEGILDIR -- rollback sim icinde KULLANILAMAZLAR. Bu, hicbir buyuk
// motorun sunamadigi bir bilesim.
//
// Sigmoid/Tanh de saglanir (genel amacli kullanim icin) ama ACIKCA
// UYARILIR: std::exp/std::tanh KULLANIRLAR, platformlar arasi bit-es
// GARANTI EDILMEZ -- KESIN determinizm gereken yerlerde (rollback sim
// icinde calisan aglar) YALNIZ ReLU/None kullanin.
#pragma once
#include <cstdint>

namespace tulpar::engine {

enum class Activation : uint8_t {
  kNone,    // dogrusal (regresyon/son katman) -- DETERMINISTIK
  kReLU,    // max(0,x) -- DETERMINISTIK (libm YOK, rollback-sim GUVENLI)
  kSigmoid, // 1/(1+exp(-x)) -- std::exp KULLANIR, platformlar arasi BIT-ES DEGIL
  kTanh,    // std::tanh KULLANIR, platformlar arasi BIT-ES DEGIL
};

struct DenseLayer {
  const float *weights = nullptr; // [out_dim x in_dim], satir-major: weights[o*in_dim+i]
  const float *bias = nullptr;    // [out_dim], nullptr ise bias YOK (0 varsayilir)
  uint32_t in_dim = 0, out_dim = 0;
};

// output: cagiranin ayirdigi out_dim boyutlu dizi. input/output AYNI
// pointer OLAMAZ (her katmanin GIRISI bir ONCEKI KATMANIN CIKISIDIR, ayri
// arabellek gerekir -- yerinde guncelleme desteklenmez).
void dense_forward(const DenseLayer &layer, const float *input, float *output, Activation act);

// Birden fazla DenseLayer'i ZINCIRLER (klasik "Sequential" model). Ayirma
// yok (A2): cagiran SABIT boyutlu bir scratch arabellek verir.
struct Sequential {
  static constexpr uint32_t kMaxLayers = 8;
  DenseLayer layers[kMaxLayers];
  Activation activations[kMaxLayers] = {};
  uint32_t layer_count = 0;

  bool add_layer(const DenseLayer &layer, Activation act) {
    if (layer_count >= kMaxLayers) return false;
    layers[layer_count] = layer;
    activations[layer_count] = act;
    layer_count++;
    return true;
  }

  // scratch/scratch_capacity: PING-PONG arabellek icin -- cagiran EN AZ
  // 2 * (agdaki EN GENIS ARA katmanin out_dim'i) kadar float ayirmalidir
  // (giris/cikis katmanlari HARIC, onlar `input`/`output` parametrelerini
  // kullanir). Donus: son katmanin out_dim'i (0 = bos model, hicbir sey yapilmadi).
  uint32_t forward(const float *input, float *output, float *scratch, uint32_t scratch_capacity) const;
};

} // namespace tulpar::engine
