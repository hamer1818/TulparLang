// content/occlusion.hpp: standart z-buffer "yakin kazanir" kuralini ve
// "TUM hucreler gizliyse occluded" tanimini elle kurgulanmis kucuk bir
// tamponda dogrular.
#include "content/occlusion.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine::content;

ENGINE_TEST(occlusion_fully_covered_candidate_behind_occluder_is_occluded) {
  float cells[16];
  DepthBuffer db{cells, 4, 4};
  clear_depth_buffer(db, 1.0f);
  rasterize_occluder_rect(db, 0, 0, 4, 4, 0.5f); // TUM tampon, occluder derinligi 0.5

  CHECK(is_occluded(db, 1, 1, 3, 3, 0.8f));  // aday DAHA UZAK (0.8>0.5) -> gizli
  CHECK(!is_occluded(db, 1, 1, 3, 3, 0.3f)); // aday DAHA YAKIN (0.3<0.5) -> gorunur
}

ENGINE_TEST(occlusion_partially_covered_candidate_is_not_occluded) {
  float cells[16];
  DepthBuffer db{cells, 4, 4};
  clear_depth_buffer(db, 1.0f);
  rasterize_occluder_rect(db, 0, 0, 2, 4, 0.5f); // yalniz SOL yarim (x:0-2)

  // Aday TUM tamponu kapliyor (x:0-4), derinligi 0.8: sol yarida occluder
  // (0.5) daha yakin ama SAG yarida hala 'far'(1.0) -- kismi gizlenme
  // TOPLAM occlusion SAYILMAZ (en az bir hucre gorunur).
  CHECK(!is_occluded(db, 0, 0, 4, 4, 0.8f));
}

ENGINE_TEST(occlusion_rect_entirely_outside_buffer_is_never_occluded) {
  float cells[16];
  DepthBuffer db{cells, 4, 4};
  clear_depth_buffer(db, 1.0f);
  rasterize_occluder_rect(db, 0, 0, 4, 4, 0.1f); // her yer occluded olsa BILE
  CHECK(!is_occluded(db, -10, -10, -5, -5, 0.5f)); // tampon disinda -> guvenli varsayim
}

ENGINE_TEST(occlusion_closer_occluder_overwrites_farther_one) {
  float cells[4];
  DepthBuffer db{cells, 2, 2};
  clear_depth_buffer(db, 1.0f);
  rasterize_occluder_rect(db, 0, 0, 2, 2, 0.5f);
  rasterize_occluder_rect(db, 0, 0, 2, 2, 0.9f); // DAHA UZAK ikinci occluder -> hucreyi DEGISTIRMEMELI
  CHECK(is_occluded(db, 0, 0, 2, 2, 0.7f)); // hala 0.5 ile karsilastirilmali (0.7>0.5 -> gizli)
  CHECK(!is_occluded(db, 0, 0, 2, 2, 0.4f)); // 0.4<0.5 -> gorunur
}
