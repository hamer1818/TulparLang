// L1 CORE — Ikili sinirlayici hacim hiyerarsisi (Bounding Volume Hierarchy).
// Cok sayida nesne arasinda HIZLI en-yakin-isabet ray sorgusu icin standart
// veri yapisi (secim/atis-testi/gorus-hatti icin dogrusal tarama yerine
// O(log n)). core/math/vec.hpp'nin ZATEN test edilmis Ray/Aabb kesisim
// fonksiyonunu (Aabb intersect + slab yontemi, tests/test_math.cpp'de
// dogrulandi) DOGRUDAN kullanir, kendi kesisim matematigini turetmez.
//
// **Insa: SAH (Surface Area Heuristic), SIFIRDAN TASARIM DEGIL, GERCEK bir
// PORT.** Maliyet formulu NanoRT'tan (MIT lisansli, github.com/
// lighttransport/nanort, tek-basliklik ray tracing cekirdegi) birebir
// alindi: cost(i) = sol_sayi*sol_alan + sag_sayi*sag_alan (gecis/kesisim
// sabitleri minimizasyonda alakasiz -- NanoRT'un kendi yorumu, bkz.
// nanort.h "Traversal cost and intersection cost are irrelevant for
// minimization"). ONARILAN/BASITLESTIRILEN kisim ARAMA stratejisi: NanoRT
// sabit-sayida KUTU (binning) kullanir (buyuk n icin performans
// optimizasyonu); burada TAM siralama+sweep (her 3 eksende TUM olasi
// bolme noktalari denenir) kullanilir -- kucuk n'de (mobil sahnelerde
// tipik nesne sayisi) OPTIMAL sonucu ureten, iyi bilinen esdeger bir
// alternatif, VE derleyicisiz ortamda ELLE hand-trace ile TAM
// dogrulanabilir (binning'in yaklasik kutu sinirlarini elle izlemek
// pratik degildir). std::stable_sort KULLANILIR (std::sort degil): esit
// merkezli ogelerde bile sonuc HER ZAMAN ayni sirada kalir -- bu, "3
// platform bit-es" determinizm ilkesiyle AYNI cizgide, derleyici/stdlib
// uygulama detayina birakilmaz.
//
// Sorgu: node'un kutusuna giris mesafesi (t) zaten bulunmus en iyi
// sonuçtan BUYUKSE alt-agac BUDANIR (bir AABB'nin ICINDEKI hicbir nokta
// giris mesafesinden DAHA YAKIN olamaz -- standart, kanitlanmis BVH
// budama ilkesi).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine {

struct BvhNode {
  Aabb bounds;
  uint32_t left = UINT32_MAX, right = UINT32_MAX; // ic dugum: cocuk indeksleri. YAPRAK: ikisi de UINT32_MAX.
  uint32_t first = 0, count = 0; // YALNIZ yapraklarda anlamli: item_order[first..first+count) bu dugumdeki ogeler
};

class Bvh {
 public:
  // item_bounds: item_count adet AABB -- BVH TARAFINDAN KOPYALANMAZ, yalniz
  // build() SIRASINDA okunur. CAGIRAN bu diziyi (AYNI ICERIK/SIRAYLA)
  // sorgularda (raycast_closest) DA vermelidir -- item indeksleri o diziye
  // GORE anlamlidir. leaf_threshold=0 -> 1 sayilir (en az bir oge/yaprak).
  bool build(Arena &arena, const Aabb *item_bounds, uint32_t item_count, uint32_t leaf_threshold = 4);

  // En YAKIN (en kucuk t>=0) kesisen ogenin indeksini + mesafesini bulur.
  // Donus false: agac bos ya da hicbir kesisim yok.
  bool raycast_closest(const Aabb *item_bounds, Ray ray, uint32_t *out_item, float *out_t) const;

  uint32_t node_count() const { return node_count_; }
  bool empty() const { return node_count_ == 0; }

  // Salt-okunur ic gozlem (hata ayiklama/gorsellestirme + testler icin --
  // bkz. tests/test_bvh.cpp'nin SAH'in ortanca-bolmeden FARKLI/DAHA IYI bir
  // agac urettigini kanitlayan testi). root()==UINT32_MAX: agac bos.
  uint32_t root() const { return node_count_ > 0 ? 0 : UINT32_MAX; }
  const BvhNode &node(uint32_t idx) const { return nodes_[idx]; }
  uint32_t item_order(uint32_t i) const { return item_order_[i]; }

 private:
  uint32_t build_recursive(const Aabb *item_bounds, uint32_t *order, uint32_t start, uint32_t end,
                            uint32_t leaf_threshold);
  // [start,end) araligini TUM 3 eksende degerlendirir, en dusuk SAH
  // maliyetli (eksen, bolme-indeksi) ciftini bulur -- order[] DEGISTIRMEZ.
  void find_best_sah_split(const Aabb *item_bounds, const uint32_t *order, uint32_t start, uint32_t end,
                            int *out_axis, uint32_t *out_split);
  void raycast_node(const Aabb *item_bounds, uint32_t node_idx, Ray ray, uint32_t *best_item, float *best_t) const;

  BvhNode *nodes_ = nullptr;
  uint32_t node_count_ = 0;
  uint32_t *item_order_ = nullptr; // build() sirasinda item_count boyutunda ayrilir, KALICI tutulur

  // SAH degerlendirme calisma alani: build() SIRASINDA (item_count
  // boyutunda) BIR KEZ ayrilir, TUM recursive build_recursive cagrilari
  // (aymı anda TEK BIRI aktif, tekli-iş parçacıklı insa) tarafindan
  // YENIDEN KULLANILIR -- insa SONRASI sifir ayirma ilkesiyle AYNI cizgide.
  uint32_t *scratch_ = nullptr;
  float *prefix_area_ = nullptr;
  float *suffix_area_ = nullptr;
};

} // namespace tulpar::engine
