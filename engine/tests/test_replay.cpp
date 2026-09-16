// sim/replay.hpp: kayit->serilestir->yukle->oynat dongusunun BAYT BIREBIR
// ayni girdileri geri verdigini kanitlar -- bu, "replay dosyasi = TAM ve
// KUCUK bir tekrar-uretim" iddiasinin TEK gerekcesidir.
#include <cstdint>
#include <cstring>

#include "core/memory/arena.hpp"
#include "sim/replay.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
struct TestInput {
  int32_t dx = 0;
  int32_t dy = 0;
};
} // namespace

ENGINE_TEST(replay_record_serialize_load_roundtrip_byte_exact) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "replay1"));
  ReplayRecorder<TestInput> rec;
  CHECK(rec.init(sys, 16));

  const TestInput inputs[5] = {{1, 2}, {-3, 4}, {0, 0}, {100, -100}, {7, 7}};
  for (const auto &in : inputs) CHECK(rec.record(in));
  CHECK(rec.frame_count() == 5);

  const uint64_t sz = rec.serialized_size();
  CHECK(sz == sizeof(ReplayHeader) + 5 * sizeof(TestInput));
  uint8_t buf[512];
  CHECK(sz <= sizeof(buf));
  rec.serialize(buf);

  ReplayPlayer<TestInput> player;
  CHECK(player.load(buf, sz));
  CHECK(player.frame_count() == 5);
  for (uint32_t i = 0; i < 5; i++) {
    TestInput out;
    CHECK(player.get_input(i, &out));
    CHECK(out.dx == inputs[i].dx && out.dy == inputs[i].dy);
  }
  TestInput dummy;
  CHECK(!player.get_input(5, &dummy)); // sinir disi kare -> false
}

ENGINE_TEST(replay_recorder_rejects_when_capacity_full) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "replay2"));
  ReplayRecorder<TestInput> rec;
  CHECK(rec.init(sys, 3));
  CHECK(rec.record(TestInput{1, 1}));
  CHECK(rec.record(TestInput{2, 2}));
  CHECK(rec.record(TestInput{3, 3}));
  CHECK(!rec.record(TestInput{4, 4})); // kapasite doldu -- reddedildi
  CHECK(rec.frame_count() == 3);       // fazladan kare EKLENMEDI
}

ENGINE_TEST(replay_player_rejects_invalid_magic) {
  uint8_t garbage[sizeof(ReplayHeader) + 8] = {0};
  ReplayPlayer<TestInput> player;
  CHECK(!player.load(garbage, sizeof(garbage)));
}

ENGINE_TEST(replay_player_rejects_input_size_mismatch) {
  // Farkli bir Input turuyle kaydedilmis gibi davranan bir header (input_size
  // yanlis) -- surum/tip uyusmazligi SESSIZCE yanlis yorumlanmamali.
  ReplayHeader header{kReplayMagic, 1, 2, sizeof(TestInput) + 4}; // YANLIS boyut
  uint8_t buf[sizeof(ReplayHeader) + 2 * sizeof(TestInput)] = {0};
  std::memcpy(buf, &header, sizeof(header));
  ReplayPlayer<TestInput> player;
  CHECK(!player.load(buf, sizeof(buf)));
}

ENGINE_TEST(replay_player_rejects_truncated_buffer) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "replay3"));
  ReplayRecorder<TestInput> rec;
  CHECK(rec.init(sys, 4));
  CHECK(rec.record(TestInput{1, 1}));
  CHECK(rec.record(TestInput{2, 2}));
  const uint64_t sz = rec.serialized_size();
  uint8_t buf[256];
  CHECK(sz <= sizeof(buf));
  rec.serialize(buf);

  ReplayPlayer<TestInput> player;
  // Gercek boyuttan KUCUK bir deger vererek "kesilmis dosya" simule edilir.
  CHECK(!player.load(buf, sz - 1));
}
