// core/math/bvh.hpp: en onemli ozellik "AGAC SEKLINDEN BAGIMSIZ DOGRU
// SONUC" -- testler BILEREK leaf_threshold=1 kullanarak agacin GERCEKTEN
// dallandigini garanti eder, sonra en yakin isabetin (dogrusal taramayla
// AYNI sonucu verecek sekilde) DOGRU secildigini kanitlar.
#include "core/math/bvh.hpp"
#include "core/memory/arena.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(bvh_single_item_hit_and_miss) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "bvh1"));
  Aabb box{{-1, -1, -1}, {1, 1, 1}};
  Bvh bvh;
  CHECK(bvh.build(sys, &box, 1));
  CHECK(!bvh.empty());

  uint32_t item;
  float t;
  CHECK(bvh.raycast_closest(&box, Ray{{-5, 0, 0}, {1, 0, 0}}, &item, &t));
  CHECK(item == 0);
  CHECK(nearly_equal(t, 4.0f, 1e-4f)); // -5'ten kutunun yakin yuzune (-1) 4 birim

  CHECK(!bvh.raycast_closest(&box, Ray{{-5, 5, 0}, {1, 0, 0}}, &item, &t)); // kutunun ustunden gecer
}

ENGINE_TEST(bvh_picks_globally_closest_item_with_forced_branching) {
  // leaf_threshold=1 -> UC oge de AYRI yapraklarda, agac GERCEKTEN dallanir.
  // boxA merkez x=10 (t=9), boxB merkez x=5 (t=4 -- EN YAKIN), boxC merkez
  // x=20 (t=19). Ucu de AYNI ray tarafindan kesiliyor, en yakini (B,
  // indeks 1) secilmeli -- agacin ic yapisindan BAGIMSIZ, saf dogrulugun kaniti.
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "bvh2"));
  Aabb boxes[3] = {
      {{9, -1, -1}, {11, 1, 1}},   // A: merkez x=10
      {{4, -1, -1}, {6, 1, 1}},    // B: merkez x=5 (EN YAKIN)
      {{19, -1, -1}, {21, 1, 1}},  // C: merkez x=20
  };
  Bvh bvh;
  CHECK(bvh.build(sys, boxes, 3, /*leaf_threshold=*/1));

  uint32_t item;
  float t;
  CHECK(bvh.raycast_closest(boxes, Ray{{0, 0, 0}, {1, 0, 0}}, &item, &t));
  CHECK(item == 1); // B
  CHECK(nearly_equal(t, 4.0f, 1e-4f));
}

ENGINE_TEST(bvh_ray_missing_all_items_returns_false) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "bvh3"));
  Aabb boxes[3] = {
      {{9, -1, -1}, {11, 1, 1}},
      {{4, -1, -1}, {6, 1, 1}},
      {{19, -1, -1}, {21, 1, 1}},
  };
  Bvh bvh;
  CHECK(bvh.build(sys, boxes, 3, 1));
  uint32_t item;
  float t;
  CHECK(!bvh.raycast_closest(boxes, Ray{{0, 100, 0}, {1, 0, 0}}, &item, &t)); // TUM kutulardan yukaridan gecer
}

ENGINE_TEST(bvh_empty_build_is_safe) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "bvh4"));
  Bvh bvh;
  CHECK(bvh.build(sys, nullptr, 0));
  CHECK(bvh.empty());
  uint32_t item;
  float t;
  CHECK(!bvh.raycast_closest(nullptr, Ray{{0, 0, 0}, {1, 0, 0}}, &item, &t));
}

// SAH (bkz. bvh.hpp basligindaki NanoRT portu) ORTANCA-bolmeden (sayiya
// gore 2+2) FARKLI, DAHA IYI bir agac uretmeli: box0,box1,box2 x-ekseninde
// BITISIK (kompakt), box3 x=100'de UZAK bir aykiri deger. Ortanca-bolme
// box2'yi (bitisik grubun bir parcasi) box3 (uzak) ile AYNI dala koyardi
// -- devasa, israf edilen bos hacimli bir kutu (maliyet 816). SAH bunun
// yerine box3'u TEK BASINA ayirir (maliyet 48) -- klasik SAH-vs-median
// senaryosu (Wald 2007), sayilar ELLE hesaplanip dogrulandi (yorumlarda).
ENGINE_TEST(bvh_sah_isolates_distant_outlier_unlike_median_split) {
  const Aabb boxes[4] = {
      {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},    // box0: x merkezi 0
      {{0.5f, -0.5f, -0.5f}, {1.5f, 0.5f, 0.5f}},     // box1: x merkezi 1
      {{1.5f, -0.5f, -0.5f}, {2.5f, 0.5f, 0.5f}},     // box2: x merkezi 2
      {{99.5f, -0.5f, -0.5f}, {100.5f, 0.5f, 0.5f}},  // box3: x merkezi 100 -- UZAK aykiri deger
  };
  SystemArena sys;
  CHECK(sys.reserve(1u << 16, "bvh_sah"));
  Bvh bvh;
  CHECK(bvh.build(sys, boxes, 4, /*leaf_threshold=*/1)); // MAKSIMUM dallanmayi ZORLA

  const uint32_t root = bvh.root();
  CHECK(root != UINT32_MAX);
  const BvhNode &r = bvh.node(root);
  CHECK(r.left != UINT32_MAX && r.right != UINT32_MAX); // ic dugum (yaprak degil)

  // Sag cocuk: box3'u TEK BASINA iceren bir yaprak olmali (i=3 bolmesi,
  // maliyet-48 -- YUKARIDAKI ELLE hesaplanan deger).
  const BvhNode &right = bvh.node(r.right);
  CHECK(right.left == UINT32_MAX); // yaprak
  CHECK(right.count == 1);
  CHECK(bvh.item_order(right.first) == 3);

  // Sol cocuk {box0,box1,box2} -- KENDI icinde tekrar SAH ile bolunur:
  // {box0} | {box1,box2} (maliyet 26, ELLE hesaplandi -- iki secenek de
  // 26'ya esit, ilk bulunan -- i=1 -- kazanir).
  const BvhNode &left = bvh.node(r.left);
  CHECK(left.left != UINT32_MAX); // ic dugum

  const BvhNode &ll = bvh.node(left.left);
  CHECK(ll.left == UINT32_MAX && ll.count == 1);
  CHECK(bvh.item_order(ll.first) == 0); // box0 yalniz

  const BvhNode &lr = bvh.node(left.right);
  CHECK(lr.left != UINT32_MAX); // {box1,box2} -- hala ic dugum, tekrar bolunur

  const BvhNode &lrl = bvh.node(lr.left);
  const BvhNode &lrr = bvh.node(lr.right);
  CHECK(lrl.left == UINT32_MAX && lrl.count == 1);
  CHECK(lrr.left == UINT32_MAX && lrr.count == 1);
  CHECK(bvh.item_order(lrl.first) == 1); // box1
  CHECK(bvh.item_order(lrr.first) == 2); // box2

  // Sekil dogru OLDUGU KADAR, raycast SONUCU da hala dogru olmali (siradan
  // testlerle AYNI garanti): box3'e giden bir ray onu bulmali.
  uint32_t item;
  float t;
  CHECK(bvh.raycast_closest(boxes, Ray{{99, 0, 0}, {1, 0, 0}}, &item, &t));
  CHECK(item == 3);
}
