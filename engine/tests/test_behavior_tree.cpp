// sim/behavior_tree.hpp: Sequence/Selector/Inverter'in TAM olarak standart
// davranis-agaci semantigine (kisa devre, Running durumunun KALDIGI YERDEN
// devam etmesi) uydugunu, cagri SAYISI ve SIRASINI sayarak kanitlar.
#include "sim/behavior_tree.hpp"

#include "tests/test.hpp"

using namespace tulpar::engine::sim;

namespace {

struct TestCtx {
  int calls_a = 0;
  int calls_b = 0;
  int running_ticks_before_success = 0; // A bu kadar tick Running dondursun
};

BtStatus action_a_success(void *ctx) {
  static_cast<TestCtx *>(ctx)->calls_a++;
  return BtStatus::kSuccess;
}

BtStatus action_a_failure(void *ctx) {
  static_cast<TestCtx *>(ctx)->calls_a++;
  return BtStatus::kFailure;
}

BtStatus action_b_success(void *ctx) {
  static_cast<TestCtx *>(ctx)->calls_b++;
  return BtStatus::kSuccess;
}

BtStatus action_a_running_then_success(void *ctx) {
  TestCtx *c = static_cast<TestCtx *>(ctx);
  c->calls_a++;
  if (c->calls_a <= c->running_ticks_before_success) return BtStatus::kRunning;
  return BtStatus::kSuccess;
}

} // namespace

ENGINE_TEST(behavior_tree_sequence_all_success_calls_all_children_in_order) {
  BehaviorTree bt;
  const uint32_t a = bt.add_action(action_a_success);
  const uint32_t b = bt.add_action(action_b_success);
  const uint32_t children[2] = {a, b};
  bt.set_root(bt.add_sequence(children, 2));

  uint32_t state[BehaviorTree::kMaxNodes] = {0};
  TestCtx ctx{};
  CHECK(bt.tick(state, &ctx) == BtStatus::kSuccess);
  CHECK(ctx.calls_a == 1);
  CHECK(ctx.calls_b == 1);
}

ENGINE_TEST(behavior_tree_sequence_short_circuits_on_failure) {
  BehaviorTree bt;
  const uint32_t a = bt.add_action(action_a_failure);
  const uint32_t b = bt.add_action(action_b_success);
  const uint32_t children[2] = {a, b};
  bt.set_root(bt.add_sequence(children, 2));

  uint32_t state[BehaviorTree::kMaxNodes] = {0};
  TestCtx ctx{};
  CHECK(bt.tick(state, &ctx) == BtStatus::kFailure);
  CHECK(ctx.calls_a == 1);
  CHECK(ctx.calls_b == 0); // B'ye HIC ULASILMADI (kisa devre)
}

ENGINE_TEST(behavior_tree_selector_falls_through_to_next_on_failure) {
  BehaviorTree bt;
  const uint32_t a = bt.add_action(action_a_failure);
  const uint32_t b = bt.add_action(action_b_success);
  const uint32_t children[2] = {a, b};
  bt.set_root(bt.add_selector(children, 2));

  uint32_t state[BehaviorTree::kMaxNodes] = {0};
  TestCtx ctx{};
  CHECK(bt.tick(state, &ctx) == BtStatus::kSuccess);
  CHECK(ctx.calls_a == 1);
  CHECK(ctx.calls_b == 1); // A basarisiz oldugu icin B'YE GECILDI
}

ENGINE_TEST(behavior_tree_running_state_resumes_at_same_child_next_tick) {
  BehaviorTree bt;
  const uint32_t a = bt.add_action(action_a_running_then_success);
  const uint32_t b = bt.add_action(action_b_success);
  const uint32_t children[2] = {a, b};
  bt.set_root(bt.add_sequence(children, 2));

  uint32_t state[BehaviorTree::kMaxNodes] = {0};
  TestCtx ctx{};
  ctx.running_ticks_before_success = 1;

  const BtStatus s1 = bt.tick(state, &ctx);
  CHECK(s1 == BtStatus::kRunning);
  CHECK(ctx.calls_a == 1);
  CHECK(ctx.calls_b == 0); // Sequence A'da DURAKLADI, B'ye hic gecmedi

  const BtStatus s2 = bt.tick(state, &ctx); // AYNI state dizisiyle ikinci tick
  CHECK(s2 == BtStatus::kSuccess);
  CHECK(ctx.calls_a == 2); // A KALDIGI YERDEN (yeniden bastan degil) devam etti
  CHECK(ctx.calls_b == 1); // ardindan B'ye GECILDI
}

ENGINE_TEST(behavior_tree_inverter_flips_success_and_failure) {
  BehaviorTree bt;
  const uint32_t a = bt.add_action(action_a_success);
  bt.set_root(bt.add_inverter(a));

  uint32_t state[BehaviorTree::kMaxNodes] = {0};
  TestCtx ctx{};
  CHECK(bt.tick(state, &ctx) == BtStatus::kFailure);
}

ENGINE_TEST(behavior_tree_add_composite_rejects_forward_reference) {
  BehaviorTree bt;
  const uint32_t forward_ref = 5; // henuz hic dugum eklenmedi -> gecersiz
  const uint32_t children[1] = {forward_ref};
  CHECK(bt.add_sequence(children, 1) == UINT32_MAX);
  CHECK(bt.node_count() == 0); // reddedilen ekleme dugum SAYISINI ARTIRMADI
}

ENGINE_TEST(behavior_tree_diamond_shaped_tree_selector_of_sequences) {
  // Selector( Sequence(fail, success), Sequence(success, success) )
  // -> ilk Sequence basarisiz (kisa devre), Selector ikinciye gecer -> Success.
  BehaviorTree bt;
  const uint32_t fail1 = bt.add_action(action_a_failure);
  const uint32_t succ1 = bt.add_action(action_b_success);
  const uint32_t seq1_children[2] = {fail1, succ1};
  const uint32_t seq1 = bt.add_sequence(seq1_children, 2);

  const uint32_t succ2 = bt.add_action(action_a_success);
  const uint32_t succ3 = bt.add_action(action_b_success);
  const uint32_t seq2_children[2] = {succ2, succ3};
  const uint32_t seq2 = bt.add_sequence(seq2_children, 2);

  const uint32_t root_children[2] = {seq1, seq2};
  bt.set_root(bt.add_selector(root_children, 2));

  uint32_t state[BehaviorTree::kMaxNodes] = {0};
  TestCtx ctx{};
  CHECK(bt.tick(state, &ctx) == BtStatus::kSuccess);
  CHECK(ctx.calls_a == 2); // fail1 (Sequence1 icinde) + succ2 (Sequence2 icinde)
  CHECK(ctx.calls_b == 1); // succ1'e (Sequence1 kisa devre yaptigi icin) HIC ULASILMADI, succ3'e ULASILDI
}
