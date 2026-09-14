#include "core/jobs/job_system.hpp"

#include <cstdio>

#if ENGINE_TRACY
#include <tracy/TracyC.h> // fiber giris/cikis: bolgeler fiber'a baglanir (goc olsa da dogru)
#endif

#include "core/profiler/profiler.hpp"
#include "platform/fatal.hpp"
#include "platform/memory.hpp"

namespace tulpar::engine {

namespace {
thread_local JobSystem::WorkerTls *g_tls = nullptr; // worker'lar icin
thread_local JobSystem::WorkerTls g_main_tls;       // worker olmayan thread'ler
} // namespace

JobSystem::WorkerTls *JobSystem::tls() {
  return g_tls ? g_tls : &g_main_tls;
}

Fiber *JobSystem::current_fiber() { return tls()->current; }
uint32_t JobSystem::current_worker() { return tls()->worker_index; }

bool JobSystem::init(Arena &arena, const JobSystemConfig &cfg) {
  cfg_ = cfg;
  uint32_t cpus = platform::cpu_count();
  worker_count_ = cfg.worker_threads ? cfg.worker_threads : (cpus > 1 ? cpus - 1 : 1);

  threads_ = arena.alloc_array_zeroed<platform::Thread>(worker_count_);
  worker_tls_ = arena.alloc_array_zeroed<WorkerTls>(worker_count_);
  fibers_ = arena.alloc_array_zeroed<FiberSlot>(cfg.fiber_count);
  free_fibers_ = arena.alloc_array<uint32_t>(cfg.fiber_count);
  queue_ = arena.alloc_array_zeroed<Job>(cfg.queue_capacity);
  ready_ = arena.alloc_array<uint32_t>(cfg.fiber_count);
  if (!threads_ || !worker_tls_ || !fibers_ || !free_fibers_ || !queue_ || !ready_)
    return false;

  // Fiber yiginlari: OS'tan tek bolge, her yiginin altinda bekci sayfa.
  size_t page = platform::os_page_size();
  stack_stride_ = align_up(cfg.fiber_stack_bytes, page) + page;
  stack_region_bytes_ = stack_stride_ * cfg.fiber_count;
  stack_region_ = static_cast<uint8_t *>(platform::os_reserve(stack_region_bytes_));
  if (!stack_region_) return false;
  for (uint32_t i = 0; i < cfg.fiber_count; i++) {
    uint8_t *lo = stack_region_ + stack_stride_ * i;
    platform::os_protect_none(lo, page); // bekci
    Fiber &f = fibers_[i].fiber;
    f.stack_lo = lo + page;
    f.stack_bytes = stack_stride_ - page;
    f.index = i;
    free_fibers_[i] = cfg.fiber_count - 1 - i;
  }
  fiber_names_ = arena.alloc_array<char>(cfg.fiber_count * 16);
  if (!fiber_names_) return false;
  for (uint32_t i = 0; i < cfg.fiber_count; i++) std::snprintf(fiber_names_ + i * 16, 16, "fiber-%u", i);
  free_top_ = cfg.fiber_count;

  running_.store(true, std::memory_order_release);
  for (uint32_t i = 0; i < worker_count_; i++) {
    worker_tls_[i].js = this;
    worker_tls_[i].worker_index = i;
    if (!platform::thread_create(threads_[i], worker_main, &worker_tls_[i], "tulpar-job"))
      return false;
  }
  return true;
}

void JobSystem::shutdown() {
  if (!running_.load(std::memory_order_acquire)) return;
  running_.store(false, std::memory_order_release);
  for (uint32_t i = 0; i < worker_count_; i++) platform::thread_join(threads_[i]);
  ENGINE_ASSERT_MSG(free_top_ == cfg_.fiber_count,
                    "job sistemi kapanirken %u fiber hala park halinde/calisiyor",
                    cfg_.fiber_count - free_top_);
  if (stack_region_) platform::os_release(stack_region_, stack_region_bytes_);
  stack_region_ = nullptr;
}

JobSystemStats JobSystem::stats() const {
  JobSystemStats s;
  s.jobs_run = st_jobs_.load();
  s.fiber_switches = st_switches_.load();
  s.waits_parked = st_parked_.load();
  s.waits_inline = st_inline_.load();
  s.fiber_migrations = st_migrations_.load();
  s.fiber_starved = st_starved_.load();
  s.jobs_inline = st_jobs_inline_.load();
  return s;
}

// ---- kuyruklar ------------------------------------------------------------

bool JobSystem::push_job(const Job &j) {
  SpinGuard g(q_lock_);
  if (q_count_ == cfg_.queue_capacity) return false;
  queue_[q_tail_] = j;
  q_tail_ = (q_tail_ + 1) % cfg_.queue_capacity;
  q_count_++;
  return true;
}

bool JobSystem::pop_job(Job *out) {
  SpinGuard g(q_lock_);
  if (q_count_ == 0) return false;
  *out = queue_[q_head_];
  q_head_ = (q_head_ + 1) % cfg_.queue_capacity;
  q_count_--;
  return true;
}

void JobSystem::push_ready(uint32_t fi) {
  SpinGuard g(r_lock_);
  ENGINE_ASSERT(r_count_ < cfg_.fiber_count);
  ready_[r_tail_] = fi;
  r_tail_ = (r_tail_ + 1) % cfg_.fiber_count;
  r_count_++;
}

bool JobSystem::pop_ready(uint32_t *fi) {
  SpinGuard g(r_lock_);
  if (r_count_ == 0) return false;
  *fi = ready_[r_head_];
  r_head_ = (r_head_ + 1) % cfg_.fiber_count;
  r_count_--;
  return true;
}

bool JobSystem::acquire_fiber(uint32_t *fi) {
  SpinGuard g(free_lock_);
  if (free_top_ == 0) return false;
  *fi = free_fibers_[--free_top_];
  return true;
}

void JobSystem::release_fiber(uint32_t fi) {
  SpinGuard g(free_lock_);
  free_fibers_[free_top_++] = fi;
}

// ---- sayac ----------------------------------------------------------------

void JobSystem::park_fiber(uint32_t fi, Counter *c, uint32_t target) {
  // Scheduler yigininda cagrilir: fiber TAMAMEN cikmis durumda, o yuzden
  // burada uyandirilmasi guvenli. Kilit altinda yeniden kontrol: kayip
  // uyandirma yok.
  SpinGuard g(c->lock_);
  if (c->value_.load(std::memory_order_acquire) <= target) {
    push_ready(fi);
    return;
  }
  ENGINE_ASSERT_MSG(c->waiter_count_ < Counter::kMaxWaiters,
                    "Counter: %u'dan fazla bekleyen fiber", Counter::kMaxWaiters);
  c->waiters_[c->waiter_count_++] = Counter::Waiter{fi, target};
}

void JobSystem::signal_counter(Counter &c) {
  SpinGuard g(c.lock_);
  uint32_t v = c.value_.fetch_sub(1, std::memory_order_acq_rel) - 1;
  uint32_t kept = 0;
  for (uint32_t i = 0; i < c.waiter_count_; i++) {
    if (v <= c.waiters_[i].target) push_ready(c.waiters_[i].fiber);
    else c.waiters_[kept++] = c.waiters_[i];
  }
  c.waiter_count_ = kept;
}

// ---- yurutme ----------------------------------------------------------------

void JobSystem::run(const JobDecl *jobs, uint32_t count, Counter *counter) {
  if (counter) counter->value_.fetch_add(count, std::memory_order_acq_rel);
  for (uint32_t i = 0; i < count; i++) {
    Job j{jobs[i], counter};
    while (!push_job(j)) platform::thread_yield(); // kapasite: worker'lar bosaltir
  }
}

void JobSystem::execute_job(const Job &job) {
  // Is grafigi: adli is profiler'da bolge olur (fiber'la birlikte gocer).
  Profiler *prof = job.decl.name ? Profiler::instance() : nullptr;
  if (prof) prof->zone_begin(job.decl.name);
  job.decl.fn(job.decl.data);
  if (prof) prof->zone_end();
  st_jobs_.fetch_add(1, std::memory_order_relaxed);
  if (job.counter) signal_counter(*job.counter);
}

// Fiber'siz baglamdan bekleme: park edilemez, o yuzden kuyruga YARDIM edilir
// (ic ice satir ici yurutme). Kuyruk bosalinca yield.
void JobSystem::wait_helping(Counter &counter, uint32_t target) {
  while (counter.value() > target) {
    Job j;
    if (pop_job(&j)) {
      st_jobs_inline_.fetch_add(1, std::memory_order_relaxed);
      execute_job(j);
    } else {
      platform::thread_yield();
    }
  }
}

void JobSystem::wait(Counter &counter, uint32_t target) {
  if (counter.value() <= target) return;
  WorkerTls *t = tls();
  if (t->current == nullptr) {
    st_inline_.fetch_add(1, std::memory_order_relaxed);
    if (t->worker_index != UINT32_MAX) {
      // Worker yigininda satir ici kosan is: yardim et (aclik kilidi cozumu).
      wait_helping(counter, target);
    } else {
      // Ana thread: worker degil, yield ile bekle.
      while (counter.value() > target) platform::thread_yield();
    }
    return;
  }
  st_parked_.fetch_add(1, std::memory_order_relaxed);
  Fiber *f = t->current;
  t->action = Action::Park;
  t->park_counter = &counter;
  t->park_target = target;
  st_switches_.fetch_add(1, std::memory_order_relaxed);
  tulpar_fiber_switch(&f->sp, t->scheduler_sp);
  // Burada devam: sayac hedefe ulasti. Belki baska bir thread'deyiz —
  // t artik gecersiz, yeniden okunmali (cagiran zaten tls() kullanir).
}

void JobSystem::job_entry(void *arg) {
  FiberSlot *slot = static_cast<FiberSlot *>(arg);
  JobSystem *js = tls()->js;
  js->execute_job(slot->job);
  // Bitti: scheduler'a don (fiber orada havuza iade edilir).
  WorkerTls *t = tls(); // gocmus olabiliriz: yeniden oku
  t->action = Action::Finish;
  js->st_switches_.fetch_add(1, std::memory_order_relaxed);
  tulpar_fiber_switch(&slot->fiber.sp, t->scheduler_sp);
  platform::fatal("bitmis fiber yeniden calistirildi"); // ulasilmaz
}

void JobSystem::resume_fiber(uint32_t fi, WorkerTls *t) {
  FiberSlot &slot = fibers_[fi];
  if (slot.last_worker != UINT32_MAX && slot.last_worker != t->worker_index)
    st_migrations_.fetch_add(1, std::memory_order_relaxed);
  slot.last_worker = t->worker_index;
  t->current = &slot.fiber;
  t->action = Action::None;
  st_switches_.fetch_add(1, std::memory_order_relaxed);
#if ENGINE_TRACY
  ___tracy_fiber_enter(fiber_names_ + fi * 16);
#endif
  tulpar_fiber_switch(&t->scheduler_sp, slot.fiber.sp);
#if ENGINE_TRACY
  ___tracy_fiber_leave();
#endif
  // Fiber geri gecti: t bu thread'in TLS'i, gecerli (scheduler gocmez).
  t->current = nullptr;
  switch (t->action) {
  case Action::Finish:
    release_fiber(fi);
    break;
  case Action::Park:
    park_fiber(fi, t->park_counter, t->park_target);
    break;
  case Action::None:
    platform::fatal("fiber eylemsiz geri dondu");
  }
  t->action = Action::None;
}

void JobSystem::worker_main(void *arg) {
  WorkerTls *t = static_cast<WorkerTls *>(arg);
  g_tls = t;
  JobSystem *js = t->js;
#if ENGINE_TRACY
  ___tracy_set_thread_name("tulpar-job");
#endif
  uint32_t idle = 0;
  while (js->running_.load(std::memory_order_acquire)) {
    uint32_t fi;
    if (js->pop_ready(&fi)) {
      idle = 0;
      js->resume_fiber(fi, t);
      continue;
    }
    Job job;
    if (js->pop_job(&job)) {
      idle = 0;
      if (!js->acquire_fiber(&fi)) {
        // Havuz tukendi: is worker'in kendi yigininda kosar. Beklerse park
        // edemez, kuyruga yardim eder (wait_helping). Kilitlenme yok.
        js->st_starved_.fetch_add(1, std::memory_order_relaxed);
        js->st_jobs_inline_.fetch_add(1, std::memory_order_relaxed);
        js->execute_job(job);
        continue;
      }
      FiberSlot &slot = js->fibers_[fi];
      slot.job = job;
      fiber_prepare(slot.fiber, job_entry, &slot);
      js->resume_fiber(fi, t);
      continue;
    }
    // Bos: once yield, uzarsa kisa uyku (guc; A4).
    if (++idle < 64) platform::thread_yield();
    else platform::thread_sleep_us(50);
  }
  g_tls = nullptr;
}

} // namespace tulpar::engine
