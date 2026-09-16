// L1 CORE — Ikili sinirlayici hacim hiyerarsisi (Bounding Volume Hierarchy).
// Cok sayida nesne arasinda HIZLI en-yakin-isabet ray sorgusu icin standart
// veri yapisi (secim/atis-testi/gorus-hatti icin dogrusal tarama yerine
// O(log n)). core/math/vec.hpp'nin ZATEN test edilmis Ray/Aabb kesisim
// fonksiyonunu (Aabb intersect + slab yontemi, tests/test_math.cpp'de
// dogrulandi) DOGRUDAN kullanir, kendi kesisim matematigini turetmez.
//
// Insa: ortanca-bolme (median split), en genis eksende, std::nth_element
// (standart kutuphane, dengeli bolme GARANTISI) ile -- SAH (surface area
// heuristic) gibi daha karmasik/optimal yontemler kasitli disarida
// birakildi (ilk dilim, daha basit ama HER ZAMAN dogru sonuc verir).
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

 private:
  uint32_t build_recursive(const Aabb *item_bounds, uint32_t *order, uint32_t start, uint32_t end,
                            uint32_t leaf_threshold);
  void raycast_node(const Aabb *item_bounds, uint32_t node_idx, Ray ray, uint32_t *best_item, float *best_t) const;

  BvhNode *nodes_ = nullptr;
  uint32_t node_count_ = 0;
  uint32_t *item_order_ = nullptr; // build() sirasinda item_count boyutunda ayrilir, KALICI tutulur
};

} // namespace tulpar::engine
