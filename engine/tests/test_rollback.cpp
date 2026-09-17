// sim/rollback.hpp: World::snapshot/restore uzerine kurulu rollback netcode
// cekirdegi (500 madde listesi #5/#431). Bu testler GGPO/GGRS'in standart
// iddiasini (yanlis tahmin edilen bir kare, DOGRU girdiyle yeniden oynatilinca
// tam olarak "en bastan dogru girdiyle simule edilmis" sonucu verir) somut
// sayilarla kanitlar -- rollback'in TEK degeri budur, kanitlanmazsa anlamsizdir.
#include "core/math/vec.hpp" // Vec3 (PR #322: eksikti, derlenmedigi icin gorulmemis)
#include "core/memory/arena.hpp"
#include "sim/ecs.hpp"
#include "sim/rollback.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
struct Pos { Vec3 p; };
struct Input { int32_t dx = 0; };
ComponentId cid_pos_rb() { return component_id<Pos>("PosRollback"); }

void step(World &w, Entity e, const Input &local, const Input &remote) {
  w.get<Pos>(e)->p.x += static_cast<float>(local.dx + remote.dx);
}
} // namespace

ENGINE_TEST(rollback_no_misprediction_matches_live_simulation) {
  SystemArena sys;
  CHECK(sys.reserve(4u << 20, "rb_nomiss"));
  World w;
  WorldConfig wc;
  wc.max_entities = 8;
  wc.max_chunks = 2;
  CHECK(w.init(sys, wc));
  Entity e = w.create(mask_of(cid_pos_rb()));
  CHECK(e.valid());

  RollbackBuffer<Input> rb;
  CHECK(rb.init(sys, w.snapshot_bytes(), 8));

  for (uint64_t f = 0; f < 5; f++) {
    Input local{1}, predicted{2};
    rb.save(w, local, predicted);
    step(w, e, local, predicted);
    // Uzak girdi HEMEN dogrulaniyor (tahminle ayni) -- gercek netcode'da
    // paket gecikmesiyle daha sonra gelir, burada senkron test icin hemen.
    bool changed = rb.apply_remote_input(f, predicted);
    CHECK(!changed);
  }
  CHECK(!rb.needs_resimulation());
  CHECK(w.get<Pos>(e)->p.x == 15.0f); // 5 kare * (1+2)
}

ENGINE_TEST(rollback_misprediction_resimulate_produces_corrected_state) {
  SystemArena sys;
  CHECK(sys.reserve(4u << 20, "rb_miss"));
  World w;
  WorldConfig wc;
  wc.max_entities = 8;
  wc.max_chunks = 2;
  CHECK(w.init(sys, wc));
  Entity e = w.create(mask_of(cid_pos_rb()));
  CHECK(e.valid());

  RollbackBuffer<Input> rb;
  CHECK(rb.init(sys, w.snapshot_bytes(), 8));

  auto step_fn = [e](World &world, const Input &local, const Input &remote) {
    step(world, e, local, remote);
  };

  // 5 kare, HEPSI {2} tahminiyle canli simule edilir (2. karenin GERCEK
  // degeri henuz bilinmiyor -- ag gecikmesi).
  for (uint64_t f = 0; f < 5; f++) {
    Input local{1}, predicted{2};
    rb.save(w, local, predicted);
    step_fn(w, local, predicted);
  }
  CHECK(w.get<Pos>(e)->p.x == 15.0f); // 5 * (1+2), yanlis tahminle

  // 2. karenin GERCEK uzak girdisi simdi geliyor: {2} degil {5} imis.
  bool changed = rb.apply_remote_input(2, Input{5});
  CHECK(changed);
  CHECK(rb.needs_resimulation());
  CHECK(rb.resimulate_from() == 2u);

  rb.resimulate(w, step_fn);

  CHECK(!rb.needs_resimulation());
  // Elle hesap: kare0 x=3, kare1 x=6, kare2 DUZELTILMIS(1+5=6) x=12,
  // kare3 x=15, kare4 x=18 -- canli (yanlis) sonuctan (15) TAM OLARAK
  // duzeltme farki kadar (5-2=3) fazla: bu rollback'in butun degeridir.
  CHECK(w.get<Pos>(e)->p.x == 18.0f);
}

ENGINE_TEST(rollback_confirmed_prediction_marks_confirmed_without_resim) {
  SystemArena sys;
  CHECK(sys.reserve(4u << 20, "rb_confirm"));
  World w;
  WorldConfig wc;
  wc.max_entities = 8;
  wc.max_chunks = 2;
  CHECK(w.init(sys, wc));
  Entity e = w.create(mask_of(cid_pos_rb()));
  CHECK(e.valid());

  RollbackBuffer<Input> rb;
  CHECK(rb.init(sys, w.snapshot_bytes(), 8));

  Input local{1}, predicted{2};
  rb.save(w, local, predicted);
  step(w, e, local, predicted);

  CHECK(!rb.is_confirmed(0));
  bool changed = rb.apply_remote_input(0, predicted); // tahminle AYNI
  CHECK(!changed);
  CHECK(!rb.needs_resimulation());
  CHECK(rb.is_confirmed(0));
}

ENGINE_TEST(rollback_out_of_window_input_is_rejected) {
  SystemArena sys;
  CHECK(sys.reserve(4u << 20, "rb_window"));
  World w;
  WorldConfig wc;
  wc.max_entities = 8;
  wc.max_chunks = 2;
  CHECK(w.init(sys, wc));
  Entity e = w.create(mask_of(cid_pos_rb()));
  CHECK(e.valid());

  const uint32_t kHistory = 4;
  RollbackBuffer<Input> rb;
  CHECK(rb.init(sys, w.snapshot_bytes(), kHistory));

  // kHistory'den COK daha fazla kare ilerlet: kare 0 pencereden dusmeli.
  for (uint64_t f = 0; f < 10; f++) {
    Input local{1}, predicted{0};
    rb.save(w, local, predicted);
    step(w, e, local, predicted);
  }
  CHECK(!rb.in_window(0));
  CHECK(rb.oldest_frame() == rb.current_frame() - kHistory);
  bool changed = rb.apply_remote_input(0, Input{99}); // cok gec kalmis duzeltme
  CHECK(!changed); // sessizce yok sayildi, TASMADI/bozmadi
  CHECK(!rb.needs_resimulation());
}
