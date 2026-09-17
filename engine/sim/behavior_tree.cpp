#include "sim/behavior_tree.hpp"

namespace tulpar::engine::sim {

uint32_t BehaviorTree::add_action(BtTickFn fn) {
  if (node_count_ >= kMaxNodes) return UINT32_MAX;
  const uint32_t idx = node_count_++;
  nodes_[idx].type = BtNodeType::kAction;
  nodes_[idx].action = fn;
  nodes_[idx].child_count = 0;
  return idx;
}

uint32_t BehaviorTree::add_composite(BtNodeType type, const uint32_t *children, uint32_t count) {
  if (node_count_ >= kMaxNodes || count > kMaxChildren) return UINT32_MAX;
  // Yapisal dongu-korumasi: her cocuk ONCEDEN eklenmis (kucuk indeksli) bir
  // dugume isaret ETMEK ZORUNDA -- core/jobs/job_graph.hpp'deki AYNI kural.
  for (uint32_t i = 0; i < count; i++) {
    if (children[i] >= node_count_) return UINT32_MAX;
  }
  const uint32_t idx = node_count_++;
  nodes_[idx].type = type;
  nodes_[idx].action = nullptr;
  nodes_[idx].child_count = count;
  for (uint32_t i = 0; i < count; i++) nodes_[idx].children[i] = children[i];
  return idx;
}

uint32_t BehaviorTree::add_sequence(const uint32_t *children, uint32_t count) {
  return add_composite(BtNodeType::kSequence, children, count);
}

uint32_t BehaviorTree::add_selector(const uint32_t *children, uint32_t count) {
  return add_composite(BtNodeType::kSelector, children, count);
}

uint32_t BehaviorTree::add_inverter(uint32_t child) {
  if (child >= node_count_) return UINT32_MAX;
  return add_composite(BtNodeType::kInverter, &child, 1);
}

BtStatus BehaviorTree::tick(uint32_t *state, void *context) const {
  if (root_ == UINT32_MAX) return BtStatus::kFailure;
  return tick_node(root_, state, context);
}

BtStatus BehaviorTree::tick_node(uint32_t node_idx, uint32_t *state, void *context) const {
  const BtNode &n = nodes_[node_idx];
  switch (n.type) {
    case BtNodeType::kAction:
      return n.action ? n.action(context) : BtStatus::kFailure;

    case BtNodeType::kSequence: {
      uint32_t &i = state[node_idx];
      while (i < n.child_count) {
        const BtStatus s = tick_node(n.children[i], state, context);
        if (s == BtStatus::kRunning) return BtStatus::kRunning; // KALDIGI YERDE (i sabit)
        if (s == BtStatus::kFailure) {
          i = 0; // bir sonraki tick BASTAN baslasin
          return BtStatus::kFailure;
        }
        i++; // basarili -> bir sonraki cocuga gec
      }
      i = 0; // TUMU basarili -> bir sonraki tick BASTAN baslasin
      return BtStatus::kSuccess;
    }

    case BtNodeType::kSelector: {
      uint32_t &i = state[node_idx];
      while (i < n.child_count) {
        const BtStatus s = tick_node(n.children[i], state, context);
        if (s == BtStatus::kRunning) return BtStatus::kRunning;
        if (s == BtStatus::kSuccess) {
          i = 0;
          return BtStatus::kSuccess;
        }
        i++; // basarisiz -> bir sonraki cocuga gec
      }
      i = 0; // TUMU basarisiz -> bir sonraki tick BASTAN baslasin
      return BtStatus::kFailure;
    }

    case BtNodeType::kInverter: {
      const BtStatus s = tick_node(n.children[0], state, context);
      if (s == BtStatus::kSuccess) return BtStatus::kFailure;
      if (s == BtStatus::kFailure) return BtStatus::kSuccess;
      return BtStatus::kRunning;
    }
  }
  return BtStatus::kFailure;
}

} // namespace tulpar::engine::sim
