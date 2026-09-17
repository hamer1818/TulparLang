// L4 SIMULATION — Basit davranis agaci (Behavior Tree). DEVAM_PLANI.md
// Faz B madde 6'nin karsiligi: "Basit davranis sistemi (AI) — dil
// baglamasi olmadan, C++ tarafinda basit bir state machine veya behavior
// tree. Navmesh zaten var (yol bulma); eksik olan 'ne zaman saldir/kac/
// devriye gez' karari."
//
// **DOGRULANDI, VARSAYILMADI:** Sequence/Selector/Inverter'in tick()
// semantigi, BehaviorTree.CPP'nin (MIT, github.com/BehaviorTree/
// BehaviorTree.CPP, 4200+ yildiz -- C++ icin FIILEN standart BT
// kutuphanesi) GERCEK kaynak kodu (src/controls/sequence_node.cpp,
// fallback_node.cpp, src/decorators/inverter_node.cpp) SATIR SATIR
// okunarak dogrulandi: RUNNING'de index SABIT kalir (ayni cocuktan devam),
// FAILURE'da (Sequence) / SUCCESS'te (Fallback/Selector) index SIFIRLANIR,
// SUCCESS'te (Sequence) / FAILURE'da (Fallback) bir sonraki cocuga
// GECILIR. Bu dosyanin tick_node() fonksiyonu bu davranisla BIREBIR
// eslesir. Kasitli DISARIDA birakilan (BehaviorTree.CPP'de olan ama
// burada olmayan): SKIPPED durumu, async/wake-up sinyali (coklu-is-
// parcaciği) -- Tulpar'in tek-is-parcacikli, senkron simulasyon adimiyla
// ALAKASIZ.
//
// Veri-yonelimli tasarim: agac TANIMI (BehaviorTree) SABIT/PAYLASILIR,
// calisma-zamani DURUMU (hangi Sequence/Selector "kaldigi" cocuk) CAGIRAN
// TARAFINDAN ajan basina AYRI tutulur (`state` dizisi) -- boylece TEK bir
// agac tanimi YUZLERCE ajan arasinda (ör. hepsi "devriye gez" davranisini
// paylasan dusmanlar) bellek COGALTMADAN PAYLASILABILIR (ECS/SoA
// felsefesiyle AYNI cizgide).
//
// Kapsam bilincli DAR: Action, Sequence, Selector, Inverter -- Repeater/
// Cooldown/Blackboard-sorgu gibi decorator'lar SONRAKI adim (Tulpar dil
// baglamasi entegrasyonuyla birlikte gelistirilecek, DEVAM_PLANI.md'nin
// bilincli kapsam disi tuttugu alan).
#pragma once
#include <cstdint>

namespace tulpar::engine::sim {

enum class BtStatus : uint8_t { kSuccess, kFailure, kRunning };

// context: cagiranin verdigi ajan/blackboard verisi -- BehaviorTree bunun
// ICERIGINI BILMEZ, yalniz action fonksiyonlarina OLDUGU GIBI iletir.
using BtTickFn = BtStatus (*)(void *context);

constexpr uint32_t kBtMaxNodes = 64;
constexpr uint32_t kBtMaxChildren = 8;

enum class BtNodeType : uint8_t { kAction, kSequence, kSelector, kInverter };

struct BtNode {
  BtNodeType type = BtNodeType::kAction;
  BtTickFn action = nullptr; // yalniz kAction icin
  uint32_t children[kBtMaxChildren] = {};
  uint32_t child_count = 0;
};

class BehaviorTree {
 public:
  static constexpr uint32_t kMaxNodes = kBtMaxNodes;
  static constexpr uint32_t kMaxChildren = kBtMaxChildren;

  // add_*() fonksiyonlari YAPRAKTAN KOKE dogru cagrilir (children, ONCEDEN
  // eklenmis -- kucuk indeksli -- dugumlere isaret ETMEK ZORUNDADIR;
  // core/jobs/job_graph.hpp'deki AYNI yapisal dongu-korumasi: ileri-
  // referans REDDEDILIR, UINT32_MAX doner, dugum EKLENMEZ).
  uint32_t add_action(BtTickFn fn);
  uint32_t add_sequence(const uint32_t *children, uint32_t count);
  uint32_t add_selector(const uint32_t *children, uint32_t count);
  uint32_t add_inverter(uint32_t child);
  void set_root(uint32_t node) { root_ = node; }
  uint32_t node_count() const { return node_count_; }

  // state: kMaxNodes boyutunda, CAGIRANIN (ajan basina AYRI) ayirdigi
  // calisma-zamani durumu -- SIFIRLANMIS baslamalidir (butun deger 0).
  // Sequence/Selector'un "kaldigi yer" (running cocugun indeksi) burada
  // saklanir; boylece bir AKSIYON Running dondugunde, BIR SONRAKI tick()
  // cagrisi AYNI cocuktan devam eder (yeniden bastan baslamaz).
  BtStatus tick(uint32_t *state, void *context) const;

 private:
  uint32_t add_composite(BtNodeType type, const uint32_t *children, uint32_t count);
  BtStatus tick_node(uint32_t node_idx, uint32_t *state, void *context) const;

  BtNode nodes_[kBtMaxNodes];
  uint32_t node_count_ = 0;
  uint32_t root_ = UINT32_MAX;
};

} // namespace tulpar::engine::sim
