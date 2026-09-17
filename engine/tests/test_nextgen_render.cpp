// renderer/nextgen_render.hpp: sanal doku sayfa tablosunun LRU davranisi
// ELLE iz surulerek (hangi istekte hangi yuva, hangi sayfa atiliyor)
// kanitlanir -- GPU/ucuncu-parti GEREKTIRMEZ, saf defter tutma.
// OcclusionCuller icin: kutuphane yokken/baslatilmadan GUVENLI tarafa
// (hicbir sey eleme) dustugu dogrulanir.
#include "renderer/nextgen_render.hpp"

#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::renderer;

ENGINE_TEST(vt_page_table_fills_free_slots_then_evicts_least_recently_used) {
  constexpr uint32_t kPages = 10;
  constexpr uint16_t kSlots = 2;
  uint16_t page_to_slot[kPages];
  uint32_t slot_to_page[kSlots];
  uint64_t slot_last_used[kSlots];

  VirtualTexturePageTable vt;
  CHECK(vt.init(kPages, kSlots, page_to_slot, slot_to_page, slot_last_used));
  CHECK(vt.resident_count() == 0);
  CHECK(vt.slot_of(5) == kVtInvalidSlot);

  // 1) Sayfa 5 -> BOS yuva 0.
  VtPageRequest r1 = vt.request(5);
  CHECK(r1.slot == 0);
  CHECK(r1.newly_loaded);
  CHECK(r1.evicted_page == UINT32_MAX);
  CHECK(vt.resident_count() == 1);

  // 2) Sayfa 7 -> BOS yuva 1.
  VtPageRequest r2 = vt.request(7);
  CHECK(r2.slot == 1);
  CHECK(r2.newly_loaded);
  CHECK(vt.resident_count() == 2);

  // 3) Sayfa 5 TEKRAR: zaten resident -> YENIDEN YUKLENMEZ, yalniz
  // LRU damgasi tazelenir (bu, 4. adimda 5'in DEGIL 7'nin atilmasini saglar).
  VtPageRequest r3 = vt.request(5);
  CHECK(r3.slot == 0);
  CHECK(!r3.newly_loaded);
  CHECK(r3.evicted_page == UINT32_MAX);
  CHECK(vt.resident_count() == 2);

  // 4) Sayfa 9: yuva YOK -> EN ESKI kullanilan (7, yuva 1) atilir.
  VtPageRequest r4 = vt.request(9);
  CHECK(r4.slot == 1);
  CHECK(r4.newly_loaded);
  CHECK(r4.evicted_page == 7); // 5 DEGIL: 3. adimda tazelenmisti
  CHECK(vt.resident_count() == 2); // tahliye sayiyi ARTIRMAZ

  CHECK(vt.slot_of(5) == 0);
  CHECK(vt.slot_of(9) == 1);
  CHECK(vt.slot_of(7) == kVtInvalidSlot); // artik resident DEGIL
}

ENGINE_TEST(vt_page_table_rejects_invalid_input) {
  uint16_t page_to_slot[4];
  uint32_t slot_to_page[2];
  uint64_t slot_last_used[2];
  VirtualTexturePageTable vt;

  CHECK(!vt.init(0, 2, page_to_slot, slot_to_page, slot_last_used));       // sifir sayfa
  CHECK(!vt.init(4, 0, page_to_slot, slot_to_page, slot_last_used));       // sifir yuva
  CHECK(!vt.init(4, 2, nullptr, slot_to_page, slot_last_used));            // dizi yok

  CHECK(vt.init(4, 2, page_to_slot, slot_to_page, slot_last_used));
  VtPageRequest bad = vt.request(99); // sinir disi sayfa
  CHECK(bad.slot == kVtInvalidSlot);
  CHECK(!bad.newly_loaded);
  CHECK(vt.resident_count() == 0);
}

ENGINE_TEST(occlusion_culler_rejects_bad_resolution_and_is_safe_when_uninitialized) {
  OcclusionCuller c;
  CHECK(!c.valid());
  // Baslatilmamis kirpici HICBIR SEYI elememeli (yanlis eleme = kaybolan
  // nesne; yanlis "gorunur" = yalniz bir miktar bosa cizim).
  CHECK(c.test_rect(-0.5f, -0.5f, 0.5f, 0.5f, 1.0f) == OcclusionResult::kVisible);

  // Kutuphanenin kurali: genislik 8'in, yukseklik 4'un KATI olmali.
  CHECK(!c.init(100, 64)); // 100 % 8 != 0
  CHECK(!c.init(96, 65));  // 65 % 4 != 0
  CHECK(!c.init(0, 0));
}
