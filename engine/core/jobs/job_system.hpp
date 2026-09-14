// L1 CORE — Fiber tabanli job sistemi (Naughty Dog, GDC 2015 modeli).
//
//   * Worker thread sayisi = cekirdek - 1 (ana thread'e yer). Yapilandirilir.
//   * Job bir fiber uzerinde kosar; `wait()` fiber'i park edip thread'i
//     BASKA ise verir. Thread bloklanmaz, mutex yok, sayac var.
//   * Butun kapasiteler init'te verilir (A2): kuyruk, fiber havuzu, yigin.
//     Kare icinde hicbir sey ayrilmaz.
//   * Fiber baska thread'de devam edebilir; TLS'i noinline getter'la oku.
//
// Fiber havuzu tukenirse is worker'in KENDI yigininda satir ici kosar ve
// onun `wait()`i park edemedigi icin kuyruktaki islere YARDIM eder (ic ice).
// Bu, park etmis fiber'larin havuzu tuketip cocuklarini bekledigi kilidi
// cozer (olculdu 2026-09-14: 8 fiber + 32 ebeveyn = sonsuz bekleme). Havuz
// boyutu yine init'te verilir (A2); aclik SAYILIR, sessiz degil.
//
// Faz 0 sinirlari (bilerek): kuyruk spinlock'lu ring (lock-free degil);
// ana thread'den `wait()` yield ile doner (ana thread worker degil).
#pragma once
#include <atomic>
#include <cstdint>

#include "core/jobs/fiber.hpp"
#include "core/jobs/spinlock.hpp"
#include "core/memory/arena.hpp"
#include "platform/thread.hpp"

namespace tulpar::engine {

using JobFn = void (*)(void *data);

struct JobDecl {
  JobFn fn = nullptr;
  void *data = nullptr;
  const char *name = nullptr; // verilirse profiler'da is basina bolge (is grafigi)
};

class JobSystem;

// Isler bitince sifira inen sayac; `wait()` hedef degere kadar bekler.
class Counter {
public:
  uint32_t value() const { return value_.load(std::memory_order_acquire); }

private:
  friend class JobSystem;
  static constexpr uint32_t kMaxWaiters = 16;
  struct Waiter {
    uint32_t fiber;
    uint32_t target;
  };
  std::atomic<uint32_t> value_{0};
  SpinLock lock_;
  Waiter waiters_[kMaxWaiters];
  uint32_t waiter_count_ = 0;
};

struct JobSystemConfig {
  uint32_t worker_threads = 0;       // 0 = cekirdek-1 (en az 1)
  uint32_t fiber_count = 128;
  // 256 KB: Jolt'un carpisma job'lari (ProcessBodyPair) 64 KB'de bekci sayfaya
  // carpiyor, 128 KB'de geciyor (olculdu 2026-09-14, 65 govde); pay birakildi.
  // Sanal rezerv: 128 x 256 KB = 32 MB, fiziksel yalniz dokunulan.
  uint32_t fiber_stack_bytes = 256 * 1024;
  uint32_t queue_capacity = 4096;
};

struct JobSystemStats {
  uint64_t jobs_run = 0;
  uint64_t fiber_switches = 0;
  uint64_t waits_parked = 0;   // fiber uzerinde park edilen bekleme
  uint64_t waits_inline = 0;   // fiber disindan (ana thread) bekleme
  uint64_t fiber_migrations = 0; // fiber baska thread'de devam etti
  uint64_t fiber_starved = 0;  // havuz tukendi, is SATIR ICI kostu
  uint64_t jobs_inline = 0;    // fiber'siz (worker yigininda) kosan is
};

class JobSystem {
public:
  bool init(Arena &arena, const JobSystemConfig &cfg);
  void shutdown();

  // `count` isi kuyruga ekler; `counter` verildiyse count kadar artirilir.
  void run(const JobDecl *jobs, uint32_t count, Counter *counter);
  void run(JobDecl job, Counter *counter) { run(&job, 1, counter); }

  // counter.value() <= target olana kadar bekler.
  void wait(Counter &counter, uint32_t target = 0);

  uint32_t worker_count() const { return worker_count_; }
  JobSystemStats stats() const;

  // Su an bir job fiber'i uzerinde miyiz? (null = ana thread / worker scheduler)
  static Fiber *current_fiber();
  // Bu thread'in worker indeksi (ana thread: UINT32_MAX)
  static uint32_t current_worker();

  // Uygulama ayrintisi (thread_local tanimlari .cpp'de; bu yuzden public).
  struct Job {
    JobDecl decl;
    Counter *counter;
  };
  enum class Action : uint8_t { None, Finish, Park };
  struct WorkerTls {
    JobSystem *js = nullptr;
    void *scheduler_sp = nullptr;
    Fiber *current = nullptr;
    Action action = Action::None;
    Counter *park_counter = nullptr;
    uint32_t park_target = 0;
    uint32_t worker_index = UINT32_MAX;
  };

private:
  struct FiberSlot {
    Fiber fiber;
    Job job;
    uint32_t last_worker = UINT32_MAX;
  };

  static void worker_main(void *arg);
  static void job_entry(void *arg);
  static WorkerTls *tls() __attribute__((noinline));

  bool pop_job(Job *out);
  bool push_job(const Job &j);
  bool pop_ready(uint32_t *fi);
  void push_ready(uint32_t fi);
  bool acquire_fiber(uint32_t *fi);
  void release_fiber(uint32_t fi);
  void execute_job(const Job &job);
  void wait_helping(Counter &counter, uint32_t target);
  void resume_fiber(uint32_t fi, WorkerTls *t);
  void park_fiber(uint32_t fi, Counter *c, uint32_t target);
  void signal_counter(Counter &c);

  JobSystemConfig cfg_{};
  std::atomic<bool> running_{false};
  uint32_t worker_count_ = 0;
  platform::Thread *threads_ = nullptr;
  WorkerTls *worker_tls_ = nullptr;

  FiberSlot *fibers_ = nullptr;
  uint32_t *free_fibers_ = nullptr; // yigin
  uint32_t free_top_ = 0;
  SpinLock free_lock_;
  uint8_t *stack_region_ = nullptr;
  size_t stack_region_bytes_ = 0;
  size_t stack_stride_ = 0;

  Job *queue_ = nullptr;
  uint32_t q_head_ = 0, q_tail_ = 0, q_count_ = 0;
  SpinLock q_lock_;

  uint32_t *ready_ = nullptr;
  uint32_t r_head_ = 0, r_tail_ = 0, r_count_ = 0;
  SpinLock r_lock_;

  std::atomic<uint64_t> st_jobs_{0}, st_switches_{0}, st_parked_{0},
      st_inline_{0}, st_migrations_{0}, st_starved_{0}, st_jobs_inline_{0};
};

} // namespace tulpar::engine
