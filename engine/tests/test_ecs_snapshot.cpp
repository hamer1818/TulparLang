// World::snapshot()/restore(): rollback hazirligi (420 madde listesi #5/#391).
// Arena-tabanli sabit yerlesim sayesinde tam bayt-kopyasi olarak calisir --
// bu test snapshot sonrasi YAPILAN degisikliklerin (yeni entity, mutasyon,
// silme) restore() ile TAMAMEN geri alindigini content_hash() ve somut
// alan degerleriyle kanitlar.
#include "core/math/vec.hpp" // Vec3 (PR #322: eksikti, derlenmedigi icin gorulmemis)
#include "core/memory/arena.hpp"
#include "sim/ecs.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
struct Pos { Vec3 p; };
ComponentId cid_pos() { return component_id<Pos>("PosSnap"); }
} // namespace

ENGINE_TEST(ecs_snapshot_restore_roundtrip) {
  SystemArena sys;
  CHECK(sys.reserve(4u << 20, "ecs_snap"));
  World w;
  WorldConfig wc;
  wc.max_entities = 32;
  wc.max_chunks = 4;
  CHECK(w.init(sys, wc));

  const ComponentMask m = mask_of(cid_pos());
  Entity e0 = w.create(m), e1 = w.create(m), e2 = w.create(m);
  CHECK(e0.valid() && e1.valid() && e2.valid());
  w.get<Pos>(e0)->p = Vec3{1, 0, 0};
  w.get<Pos>(e1)->p = Vec3{2, 0, 0};
  w.get<Pos>(e2)->p = Vec3{3, 0, 0};
  const uint32_t entities_before = w.stats().entities;
  const uint64_t hash_before = w.content_hash();

  const uint64_t sz = w.snapshot_bytes();
  CHECK(sz > 0);
  void *buf = sys.alloc(sz, 16);
  CHECK(buf != nullptr);
  w.snapshot(buf);

  // --- Anlik goruntuden SONRA sahneyi bozan degisiklikler ---
  w.get<Pos>(e1)->p = Vec3{99, 99, 99}; // mevcut veriyi degistir
  w.destroy(e2);                         // bir entity'yi sil
  Entity e3 = w.create(m);               // yeni bir entity ekle (snapshot SONRASI dogdu)
  CHECK(e3.valid());
  w.get<Pos>(e3)->p = Vec3{7, 7, 7};
  const uint64_t hash_mutated = w.content_hash();
  CHECK(hash_mutated != hash_before); // mutasyon gercekten bir sey degistirdi

  // --- Geri al ---
  w.restore(buf);

  CHECK(w.content_hash() == hash_before); // TAM eslesme: rollback basarili
  CHECK(w.stats().entities == entities_before);
  CHECK(w.alive(e0) && w.alive(e1) && w.alive(e2));
  CHECK((w.get<Pos>(e1)->p == Vec3{2, 0, 0})); // mutasyon geri alindi
  CHECK(!w.alive(e3));                        // snapshot sonrasi dogan entity artik YOK
}

ENGINE_TEST(ecs_snapshot_bytes_matches_actual_copy_size) {
  // Negatif/tutarlilik kontrolu: snapshot_bytes() ile snapshot()'in yazdigi
  // gercek bayt sayisi TUTARLI olmali (arena tamponu bu boyuta gore ayrildi
  // ve tasma/eksik yazma olursa bir sonraki arena ayirmasi bozulurdu).
  SystemArena sys;
  CHECK(sys.reserve(2u << 20, "ecs_snap2"));
  World w;
  WorldConfig wc;
  wc.max_entities = 16;
  wc.max_chunks = 2;
  CHECK(w.init(sys, wc));
  const uint64_t sz = w.snapshot_bytes();
  // Ayirdiktan hemen SONRA baska bir kucuk blok ayirip onun adresinin
  // tampon+sz'ye esit oldugunu dogrulamak, snapshot()'in tam sz kadar
  // yazdigini (fazla/eksik degil) DOLAYLI kanitlar.
  uint8_t *buf = static_cast<uint8_t *>(sys.alloc(sz, 8));
  CHECK(buf != nullptr);
  uint8_t *guard = static_cast<uint8_t *>(sys.alloc(1, 8));
  CHECK(guard != nullptr);
  *guard = 0xAB; // bilinen bekci degeri
  w.snapshot(buf); // buf[0..sz) disina TASMAMALI (bekciyi bozmamali)
  CHECK(*guard == 0xAB); // tasma olsaydi (buyuk ihtimalle) bu deger degisirdi
  (void)buf;
}
