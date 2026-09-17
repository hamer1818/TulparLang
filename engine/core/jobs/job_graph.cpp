#include "core/jobs/job_graph.hpp"

namespace tulpar::engine {

void JobGraph::reset() {
  // NOT: Node::counter (icinde std::atomic barindirir) KOPYALANAMAZ/
  // ATANAMAZ -- bu yuzden "nodes_[i] = Node{}" DERLENMEZ, alanlar TEK TEK
  // sifirlanir. counter'in KENDISINE dokunulmaz: bir onceki round'un TUM
  // isleri submit()'ten sonra js.wait(done) ile beklenmis olmali (cagiranin
  // sorumlulugu), bu durumda counter zaten 0'da -- js.run() bir sonraki
  // submit()'te yine +1 ekleyip is bitince sifira indirecek, eski deger
  // ONEMSIZ.
  node_count_ = 0;
  for (uint32_t i = 0; i < kMaxNodes; i++) {
    nodes_[i].fn = nullptr;
    nodes_[i].data = nullptr;
    nodes_[i].name = nullptr;
    nodes_[i].dep_count = 0;
  }
}

uint32_t JobGraph::add_node(JobFn fn, void *data, const uint32_t *deps, uint32_t dep_count, const char *name) {
  if (node_count_ >= kMaxNodes) return UINT32_MAX;
  if (dep_count > kMaxDeps) return UINT32_MAX;
  for (uint32_t i = 0; i < dep_count; i++) {
    // YAPISAL dongu korumasi: bir dugum SADECE kendinden ONCE eklenmis
    // (kucuk indeksli) bir dugume bagimli olabilir.
    if (deps[i] >= node_count_) return UINT32_MAX;
  }
  const uint32_t idx = node_count_++;
  Node &n = nodes_[idx];
  n.fn = fn;
  n.data = data;
  n.name = name;
  n.dep_count = dep_count;
  for (uint32_t i = 0; i < dep_count; i++) n.deps[i] = deps[i];
  return idx;
}

void JobGraph::trampoline_entry(void *arg) {
  Trampoline *t = static_cast<Trampoline *>(arg);
  Node &n = t->graph->nodes_[t->node_index];
  for (uint32_t i = 0; i < n.dep_count; i++) {
    Node &dep = t->graph->nodes_[n.deps[i]];
    t->js->wait(dep.counter, 0); // oncul TAMAMEN bitene (sayaci 0'a inene) kadar fiber PARK EDER
  }
  if (n.fn) n.fn(n.data);
  // NOT: bu dugumun KENDI counter'i BURADA elle indirilmez -- JobSystem::
  // execute_job() bu fonksiyondan DONULDUKTEN hemen sonra otomatik yapar
  // (submit()'te bu is `&n.counter` ile js.run()'a verildigi icin).
}

void JobGraph::all_done_entry(void *arg) {
  Trampoline *t = static_cast<Trampoline *>(arg);
  for (uint32_t i = 0; i < t->graph->node_count_; i++) t->js->wait(t->graph->nodes_[i].counter, 0);
}

void JobGraph::submit(JobSystem &js, Counter *done) {
  for (uint32_t i = 0; i < node_count_; i++) {
    trampolines_[i] = Trampoline{this, &js, i};
    JobDecl decl{trampoline_entry, &trampolines_[i], nodes_[i].name};
    js.run(decl, &nodes_[i].counter);
  }
  if (done) {
    all_done_tramp_ = Trampoline{this, &js, 0};
    JobDecl decl{all_done_entry, &all_done_tramp_, "job_graph_done"};
    js.run(decl, done);
  }
}

} // namespace tulpar::engine
