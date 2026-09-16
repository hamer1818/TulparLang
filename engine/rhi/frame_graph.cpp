#include "rhi/frame_graph.hpp"

namespace tulpar::engine::rhi {

void FrameGraph::copy_name(char *dst, const char *src) {
  uint32_t i = 0;
  for (; i < 31 && src && src[i]; i++) dst[i] = src[i];
  dst[i] = '\0';
}

FgResourceId FrameGraph::create_resource(const char *name) {
  if (resource_count_ >= kMaxResources) return kFgInvalidResource;
  Resource &r = resources_[resource_count_];
  copy_name(r.name, name);
  r.producer = kFgInvalidPass;
  return resource_count_++;
}

FgPassId FrameGraph::add_pass(const char *name, bool is_output) {
  if (pass_count_ >= kMaxPasses) return kFgInvalidPass;
  Pass &p = passes_[pass_count_];
  copy_name(p.name, name);
  p.read_count = 0;
  p.is_output = is_output;
  return pass_count_++;
}

bool FrameGraph::pass_writes(FgPassId pass, FgResourceId resource) {
  if (pass >= pass_count_ || resource >= resource_count_) return false;
  if (resources_[resource].producer != kFgInvalidPass) return false; // coklu-yazim reddedildi
  resources_[resource].producer = pass;
  return true;
}

bool FrameGraph::pass_reads(FgPassId pass, FgResourceId resource) {
  if (pass >= pass_count_ || resource >= resource_count_) return false;
  Pass &p = passes_[pass];
  if (p.read_count >= kMaxReadsPerPass) return false;
  p.reads[p.read_count++] = resource;
  return true;
}

bool FrameGraph::compile() {
  compiled_ = false;

  // 1) Canli kume: is_output pass'lardan GERIYE DOGRU ulasilabilirlik
  // (bir pass'in okudugu kaynagin URETICISI de canli olmak ZORUNDA).
  for (uint32_t i = 0; i < pass_count_; i++) live_[i] = false;
  FgPassId stack[kMaxPasses];
  uint32_t stack_top = 0;
  for (uint32_t i = 0; i < pass_count_; i++) {
    if (passes_[i].is_output) stack[stack_top++] = i;
  }
  while (stack_top > 0) {
    const FgPassId p = stack[--stack_top];
    if (live_[p]) continue;
    live_[p] = true;
    const Pass &pass = passes_[p];
    for (uint32_t i = 0; i < pass.read_count; i++) {
      const FgPassId producer = resources_[pass.reads[i]].producer;
      if (producer != kFgInvalidPass && !live_[producer]) stack[stack_top++] = producer;
    }
  }

  // 2) DFS tabanli topolojik siralama (yalniz canli pass'lar), 3-renkli
  // dongu tespiti (Visiting->Visiting kenar = geri-kenar = dongu).
  VisitState state[kMaxPasses];
  for (uint32_t i = 0; i < pass_count_; i++) state[i] = VisitState::kUnvisited;
  order_count_ = 0;
  for (uint32_t i = 0; i < pass_count_; i++) {
    if (live_[i] && state[i] == VisitState::kUnvisited) {
      if (!dfs_visit(i, state)) return false; // dongu -- compiled_ false kalir
    }
  }
  compiled_ = true;
  return true;
}

bool FrameGraph::dfs_visit(FgPassId p, VisitState *state) {
  state[p] = VisitState::kVisiting;
  const Pass &pass = passes_[p];
  for (uint32_t i = 0; i < pass.read_count; i++) {
    const FgPassId producer = resources_[pass.reads[i]].producer;
    if (producer == kFgInvalidPass || !live_[producer]) continue; // kaynak hic uretilmemis/canli degil
    if (state[producer] == VisitState::kVisiting) return false;   // GERI-KENAR: dongu
    if (state[producer] == VisitState::kUnvisited) {
      if (!dfs_visit(producer, state)) return false;
    }
  }
  state[p] = VisitState::kVisited;
  order_[order_count_++] = p;
  return true;
}

bool FrameGraph::resource_lifetime(FgResourceId resource, uint32_t *start, uint32_t *end) const {
  if (!compiled_ || resource >= resource_count_) return false;
  const FgPassId producer = resources_[resource].producer;
  if (producer == kFgInvalidPass) return false;

  uint32_t producer_idx = UINT32_MAX;
  for (uint32_t i = 0; i < order_count_; i++) {
    if (order_[i] == producer) { producer_idx = i; break; }
  }
  if (producer_idx == UINT32_MAX) return false; // uretici budandi -- kaynak hic kullanilmiyor

  uint32_t last = producer_idx;
  for (uint32_t i = 0; i < order_count_; i++) {
    const Pass &pass = passes_[order_[i]];
    for (uint32_t k = 0; k < pass.read_count; k++) {
      if (pass.reads[k] == resource && i > last) last = i;
    }
  }
  *start = producer_idx;
  *end = last;
  return true;
}

} // namespace tulpar::engine::rhi
