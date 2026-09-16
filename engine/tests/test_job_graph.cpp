// core/jobs/job_graph.hpp: GERCEK JobSystem + worker thread'lerle calisir
// (test_jobs.cpp'deki "jobs_nested_wait_parks_fiber_and_resumes" testinin
// ZATEN kanitladigi "is icinden wait()" desenini kullanir). Bagimlilik
// SIRASININ gercekten uygulandigini, paylasilan bir atomik SIRA sayaciyla
// (her is calistiginda +1 alip KENDI sirasini kaydeder) dogrular -- A'nin
// sirasi HER ZAMAN B'den KUCUK olmali, vb.
//
// BU MAKINEDE DERLEYICI OLMADIGI ICIN bu test BURADA CALISTIRILAMADI --
// tasarim job_system.hpp'nin belgelenmis garantilerine ve test_jobs.cpp'nin
// ZATEN kanitladigi nested-wait desenine dayanir, ama GERCEK calistirma
// ile dogrulanmis DEGILDIR.
#include <atomic>
#include <cstdint>

#include "core/jobs/job_graph.hpp"
#include "core/memory/arena.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
struct Recorder {
  std::atomic<uint32_t> *sequence_counter; // TUM dugumler arasinda PAYLASILAN sira sayaci
  std::atomic<uint32_t> *my_order;         // bu dugumun KACINCI sirada calistigi buraya yazilir
};
void record_order(void *arg) {
  Recorder *r = static_cast<Recorder *>(arg);
  const uint32_t order = r->sequence_counter->fetch_add(1, std::memory_order_acq_rel);
  r->my_order->store(order, std::memory_order_release);
}
} // namespace

ENGINE_TEST(job_graph_respects_linear_dependency_order) {
  SystemArena sys;
  CHECK(sys.reserve(8u << 20, "jg1"));
  JobSystem js;
  CHECK(js.init(sys, JobSystemConfig{}));

  std::atomic<uint32_t> seq{0};
  std::atomic<uint32_t> order_a{UINT32_MAX}, order_b{UINT32_MAX}, order_c{UINT32_MAX};
  Recorder ra{&seq, &order_a}, rb{&seq, &order_b}, rc{&seq, &order_c};

  JobGraph g;
  g.reset();
  const uint32_t a = g.add_node(record_order, &ra, nullptr, 0, "A");
  const uint32_t deps_b[1] = {a};
  const uint32_t b = g.add_node(record_order, &rb, deps_b, 1, "B");
  const uint32_t deps_c[1] = {b};
  const uint32_t c = g.add_node(record_order, &rc, deps_c, 1, "C");
  CHECK(a != UINT32_MAX && b != UINT32_MAX && c != UINT32_MAX);
  CHECK(g.node_count() == 3);

  Counter done;
  g.submit(js, &done);
  js.wait(done);

  CHECK(order_a.load() < order_b.load()); // A, B'DEN ONCE calisti
  CHECK(order_b.load() < order_c.load()); // B, C'DEN ONCE calisti
  js.shutdown();
}

ENGINE_TEST(job_graph_rejects_forward_reference_dependency) {
  JobGraph g;
  g.reset();
  const uint32_t bad_dep[1] = {5}; // henuz eklenmemis (ileri referans) dugum
  const uint32_t idx = g.add_node(record_order, nullptr, bad_dep, 1, "bad");
  CHECK(idx == UINT32_MAX); // YAPISAL olarak reddedildi, dugum EKLENMEDI (calistirilmadi -- crash riski yok)
  CHECK(g.node_count() == 0);
}

ENGINE_TEST(job_graph_diamond_dependency_runs_all_nodes_in_valid_order) {
  // A -> {B,C} -> D (D, hem B hem C bitene kadar bekler)
  SystemArena sys;
  CHECK(sys.reserve(8u << 20, "jg2"));
  JobSystem js;
  CHECK(js.init(sys, JobSystemConfig{}));

  std::atomic<uint32_t> seq{0};
  std::atomic<uint32_t> oa{UINT32_MAX}, ob{UINT32_MAX}, oc{UINT32_MAX}, od{UINT32_MAX};
  Recorder ra{&seq, &oa}, rb{&seq, &ob}, rc{&seq, &oc}, rd{&seq, &od};

  JobGraph g;
  g.reset();
  const uint32_t a = g.add_node(record_order, &ra, nullptr, 0, "A");
  const uint32_t deps1[1] = {a};
  const uint32_t b = g.add_node(record_order, &rb, deps1, 1, "B");
  const uint32_t c = g.add_node(record_order, &rc, deps1, 1, "C");
  const uint32_t deps2[2] = {b, c};
  g.add_node(record_order, &rd, deps2, 2, "D");
  CHECK(g.node_count() == 4);

  Counter done;
  g.submit(js, &done);
  js.wait(done);

  CHECK(oa.load() < ob.load());
  CHECK(oa.load() < oc.load());
  CHECK(ob.load() < od.load());
  CHECK(oc.load() < od.load());
  js.shutdown();
}
