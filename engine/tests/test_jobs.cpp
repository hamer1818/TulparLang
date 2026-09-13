#include <atomic>
#include <cstdint>
#include <cstdio>

#include "core/jobs/job_system.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "platform/thread.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
void add_one(void *d) { static_cast<std::atomic<uint64_t> *>(d)->fetch_add(1, std::memory_order_relaxed); }
void add_one_slow(void *d) {
  platform::thread_sleep_us(200);
  add_one(d);
}
struct Parent {
  JobSystem *js;
  std::atomic<uint64_t> *sum;
  std::atomic<uint32_t> *on_fiber;
};
void parent_job(void *d) {
  Parent *p = static_cast<Parent *>(d);
  if (JobSystem::current_fiber()) p->on_fiber->fetch_add(1);
  JobDecl kids[32];
  for (auto &k : kids) k = JobDecl{add_one_slow, p->sum};
  Counter c;
  p->js->run(kids, 32, &c);
  p->js->wait(c); // fiber park: cocuklar 200us uyudugu icin fast path imkansiz
  p->sum->fetch_add(1000);
}
} // namespace

ENGINE_TEST(jobs_run_all_and_count) {
  SystemArena sys;
  CHECK(sys.reserve(8u << 20, "js"));
  JobSystem js;
  CHECK(js.init(sys, JobSystemConfig{}));
  std::atomic<uint64_t> sum{0};
  JobDecl jobs[256];
  for (auto &j : jobs) j = JobDecl{add_one, &sum};
  Counter c;
  for (int r = 0; r < 40; r++) js.run(jobs, 256, &c);
  js.wait(c);
  CHECK(sum.load() == 256 * 40);
  CHECK(c.value() == 0);
  js.shutdown();
  CHECK(js.stats().jobs_run == 256 * 40);
  std::printf("    [bilgi] worker=%u goc=%llu\n", js.worker_count(),
              (unsigned long long)js.stats().fiber_migrations);
}

ENGINE_TEST(jobs_nested_wait_parks_fiber_and_resumes) {
  SystemArena sys;
  CHECK(sys.reserve(16u << 20, "js2"));
  JobSystem js;
  JobSystemConfig cfg;
  cfg.fiber_count = 96; // 64 ebeveyn park + cocuklar icin yer
  CHECK(js.init(sys, cfg));
  std::atomic<uint64_t> sum{0};
  std::atomic<uint32_t> on_fiber{0};
  Parent parents[64];
  JobDecl jobs[64];
  for (int i = 0; i < 64; i++) {
    parents[i] = Parent{&js, &sum, &on_fiber};
    jobs[i] = JobDecl{parent_job, &parents[i]};
  }
  Counter c;
  js.run(jobs, 64, &c);
  js.wait(c);
  CHECK(sum.load() == 64 * 32 + 64 * 1000);
  CHECK(on_fiber.load() == 64);
  JobSystemStats st = js.stats();
  CHECK(st.waits_parked == 64);  // her ebeveyn tam bir kez park etti
  CHECK(st.waits_inline == 1);   // ana thread'in beklemesi
  CHECK(st.jobs_run == 64 + 64 * 32);
  js.shutdown();
  std::printf("    [bilgi] gecis=%llu goc=%llu aclik=%llu\n",
              (unsigned long long)st.fiber_switches, (unsigned long long)st.fiber_migrations,
              (unsigned long long)st.fiber_starved);
}

ENGINE_TEST(jobs_fiber_pool_starvation_recovers) {
  // 8 fiber, 32 ebeveyn: havuz kesin tukenir; worker hazir fiber'lari
  // ilerleterek kurtulmali, kilitlenmemeli.
  SystemArena sys;
  CHECK(sys.reserve(8u << 20, "js3"));
  JobSystem js;
  JobSystemConfig cfg;
  cfg.fiber_count = 8;
  CHECK(js.init(sys, cfg));
  std::atomic<uint64_t> sum{0};
  std::atomic<uint32_t> on_fiber{0};
  Parent parents[32];
  JobDecl jobs[32];
  for (int i = 0; i < 32; i++) {
    parents[i] = Parent{&js, &sum, &on_fiber};
    jobs[i] = JobDecl{parent_job, &parents[i]};
  }
  Counter c;
  js.run(jobs, 32, &c);
  js.wait(c);
  CHECK(sum.load() == 32 * 32 + 32 * 1000);
  js.shutdown();
  std::printf("    [bilgi] aclik=%llu (8 fiber, 32 ebeveyn)\n",
              (unsigned long long)js.stats().fiber_starved);
}

ENGINE_TEST(jobs_zero_allocations_while_running) {
  SystemArena sys;
  CHECK(sys.reserve(8u << 20, "js4"));
  JobSystem js;
  CHECK(js.init(sys, JobSystemConfig{}));
  std::atomic<uint64_t> sum{0};
  JobDecl jobs[512];
  for (auto &j : jobs) j = JobDecl{add_one, &sum};
  AllocGate::begin_frame();
  Counter c;
  js.run(jobs, 512, &c);
  js.wait(c);
  uint64_t allocs = AllocGate::end_frame();
  CHECK(allocs == 0);
  CHECK(sum.load() == 512);
  js.shutdown();
}
