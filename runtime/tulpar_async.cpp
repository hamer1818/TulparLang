// Tulpar Async Runtime — implementation. See tulpar_async.h for the model.
//
// Stackful coroutines: POSIX via <ucontext.h>, Windows via the Fiber API.
// The scheduler runs on the "main" context; resuming a task swaps to its
// context, and the task swaps back on `await` or completion. Because tasks
// never run nested (a task always yields before another runs), a single
// shared scheduler context is sufficient.

// Platform feature-test macros must be set BEFORE any system header is pulled
// in (transitively via the project headers below), or they have no effect.
#if defined(_WIN32)
// Keep windows.h lean and stop it defining min()/max() macros that wreck the
// <algorithm>/<chrono>/<limits> we use. Mirror src/common/platform.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 // ensure the Fiber API is declared
#endif
#elif defined(__APPLE__)
// macOS hides getcontext/makecontext/swapcontext behind _XOPEN_SOURCE; in C++
// an undeclared call is a hard error (not an implicit decl). _DARWIN_C_SOURCE
// keeps the BSD extensions the rest of the runtime relies on.
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
// ucontext is deprecated on macOS but still the portable POSIX option here.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "tulpar_async.h"
#include "tulpar_arc.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#define TULPAR_ASYNC_FIBERS 1
#include <windows.h>
#else
#include <ucontext.h>
#endif

extern "C" {
void arc_retain_vmvalue(VMValue *val);
void arc_release_vmvalue(VMValue *val);
}

namespace {

constexpr size_t kCoroStackSize = 256 * 1024;

// ---- Task (coroutine) ----------------------------------------------------
struct Task {
#if TULPAR_ASYNC_FIBERS
  void *fiber = nullptr; // CreateFiber handle
#else
  ucontext_t ctx;
  char *stack = nullptr;
#endif
  void *fn = nullptr;       // user-function pointer (AOT ABI)
  VMValue *args = nullptr;  // heap copy of arguments
  int argc = 0;
  ObjPromise *result = nullptr; // promise fulfilled on return
  bool done = false;
  bool started = false;
};

// ---- Timer ---------------------------------------------------------------
struct Timer {
  long long deadline_ms;
  ObjPromise *promise;
};

// ---- Scheduler state -----------------------------------------------------
std::vector<Task *> g_ready;     // runnable tasks
std::vector<Timer> g_timers;     // pending timers (unsorted; min-scanned)
Task *g_current = nullptr;       // task currently executing (null on main)

#if TULPAR_ASYNC_FIBERS
void *g_main_fiber = nullptr;    // scheduler fiber (converted from thread)
#else
ucontext_t g_main_ctx;           // scheduler context
#endif

long long now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

// Invoke a top-level user function via the AOT ABI:
//   void fn(VMValue* ret, VMValue* arg0, VMValue* arg1, ...)
VMValue call_user_fn(void *fn, VMValue *a, int argc) {
  VMValue r;
  r.type = VM_VAL_VOID;
  r.as.int_val = 0;
  switch (argc) {
    case 0: ((void (*)(VMValue *))fn)(&r); break;
    case 1: ((void (*)(VMValue *, VMValue *))fn)(&r, &a[0]); break;
    case 2: ((void (*)(VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1]); break;
    case 3: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2]); break;
    case 4: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3]); break;
    case 5: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4]); break;
    case 6: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]); break;
    case 7: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6]); break;
    case 8: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7]); break;
    default:
      fprintf(stderr, "Tulpar async: 8'den fazla parametreli async fonksiyon desteklenmiyor / async functions with >8 params unsupported\n");
      break;
  }
  return r;
}

// The body every coroutine runs: call the user function, fulfil the promise.
void task_body(Task *t) {
  VMValue rv = call_user_fn(t->fn, t->args, t->argc);
  t->done = true;
  aot_promise_settle(t->result, rv, /*fulfilled*/ 1);
}

#if TULPAR_ASYNC_FIBERS
void CALLBACK fiber_trampoline(void *param) {
  Task *t = static_cast<Task *>(param);
  task_body(t);
  // Return control to the scheduler; the fiber will not be switched into
  // again (done==true).
  SwitchToFiber(g_main_fiber);
}
#else
// makecontext can only pass ints; stash the task in a global the trampoline
// reads on entry. Safe because tasks start one at a time under the scheduler.
Task *g_starting = nullptr;
void ctx_trampoline() {
  Task *t = g_starting;
  task_body(t);
  // Falls through to uc_link (g_main_ctx) on return.
}
#endif

// Resume a task: run it until it yields (await) or finishes.
void resume(Task *t) {
  g_current = t;
#if TULPAR_ASYNC_FIBERS
  if (!t->started) {
    t->started = true;
    t->fiber = CreateFiber(kCoroStackSize,
                           (LPFIBER_START_ROUTINE)fiber_trampoline, t);
  }
  SwitchToFiber(t->fiber);
#else
  if (!t->started) {
    t->started = true;
    getcontext(&t->ctx);
    t->stack = static_cast<char *>(malloc(kCoroStackSize));
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = kCoroStackSize;
    t->ctx.uc_link = &g_main_ctx;
    makecontext(&t->ctx, ctx_trampoline, 0);
    g_starting = t;
  }
  swapcontext(&g_main_ctx, &t->ctx);
#endif
  g_current = nullptr;
  if (t->done) {
#if TULPAR_ASYNC_FIBERS
    if (t->fiber) DeleteFiber(t->fiber);
#else
    free(t->stack);
#endif
    if (t->args) free(t->args);
    delete t;
  }
}

// Yield the currently running coroutine back to the scheduler.
void yield_to_scheduler(Task *t) {
#if TULPAR_ASYNC_FIBERS
  SwitchToFiber(g_main_fiber);
#else
  swapcontext(&t->ctx, &g_main_ctx);
#endif
}

void ensure_scheduler_inited() {
#if TULPAR_ASYNC_FIBERS
  if (!g_main_fiber) {
    g_main_fiber = ConvertThreadToFiber(nullptr);
    if (!g_main_fiber) {
      // Already a fiber (e.g. nested init) — GetCurrentFiber is valid then.
      g_main_fiber = GetCurrentFiber();
    }
  }
#endif
}

// Run one scheduler step. Returns false when there is nothing left to do.
bool loop_step() {
  if (!g_ready.empty()) {
    Task *t = g_ready.front();
    g_ready.erase(g_ready.begin());
    resume(t);
    return true;
  }
  if (!g_timers.empty()) {
    // Find the earliest deadline, sleep until it, fire all that are due.
    size_t mn = 0;
    for (size_t i = 1; i < g_timers.size(); i++)
      if (g_timers[i].deadline_ms < g_timers[mn].deadline_ms) mn = i;
    long long wait = g_timers[mn].deadline_ms - now_ms();
    if (wait > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(wait));
    long long t_now = now_ms();
    // Collect & fire all due timers (settling moves waiters onto g_ready).
    std::vector<ObjPromise *> fire;
    std::vector<Timer> keep;
    for (auto &tm : g_timers) {
      if (tm.deadline_ms <= t_now) fire.push_back(tm.promise);
      else keep.push_back(tm);
    }
    g_timers.swap(keep);
    VMValue v;
    v.type = VM_VAL_VOID;
    v.as.int_val = 0;
    for (ObjPromise *p : fire) aot_promise_settle(p, v, 1);
    return true;
  }
  return false;
}

} // namespace

// ===========================================================================
// Public C ABI
// ===========================================================================
extern "C" {

ObjPromise *aot_promise_new(void) {
  ObjPromise *p = (ObjPromise *)malloc(sizeof(ObjPromise));
  p->obj.type = OBJ_PROMISE;
  p->obj.next = nullptr;
  p->obj.arena_allocated = 1; // ARC must not reclaim while the loop holds it
  p->obj.ref_count = 1;
  p->obj.is_moved = 0;
  p->state = 0;
  p->value.type = VM_VAL_VOID;
  p->value.as.int_val = 0;
  p->waiters = nullptr;
  p->nwaiters = 0;
  p->cap_waiters = 0;
  return p;
}

void aot_promise_settle(ObjPromise *p, VMValue value, int state) {
  if (!p || p->state != 0) return; // already settled — ignore
  arc_retain_vmvalue(&value);
  p->value = value;
  p->state = state;
  // Move all waiters onto the ready queue.
  Task **w = (Task **)p->waiters;
  for (int i = 0; i < p->nwaiters; i++) g_ready.push_back(w[i]);
  if (w) free(w);
  p->waiters = nullptr;
  p->nwaiters = 0;
  p->cap_waiters = 0;
}

ObjPromise *aot_async_spawn(void *fn, VMValue *args, int argc) {
  ensure_scheduler_inited();
  Task *t = new Task();
  t->fn = fn;
  t->argc = argc;
  if (argc > 0) {
    t->args = (VMValue *)malloc(sizeof(VMValue) * argc);
    memcpy(t->args, args, sizeof(VMValue) * argc);
    for (int i = 0; i < argc; i++) arc_retain_vmvalue(&t->args[i]);
  }
  t->result = aot_promise_new();
  g_ready.push_back(t);
  return t->result;
}

int aot_is_promise(VMValue v) { return IS_PROMISE(v) ? 1 : 0; }

VMValue aot_await(VMValue awaited) {
  if (!IS_PROMISE(awaited)) return awaited; // await on a plain value is a no-op
  ObjPromise *p = AS_PROMISE(awaited);
  ensure_scheduler_inited();

  if (g_current) {
    // Inside a coroutine: register as a waiter and yield until settled.
    while (p->state == 0) {
      Task *t = g_current;
      if (p->nwaiters >= p->cap_waiters) {
        int nc = p->cap_waiters ? p->cap_waiters * 2 : 4;
        p->waiters = realloc(p->waiters, sizeof(Task *) * nc);
        p->cap_waiters = nc;
      }
      ((Task **)p->waiters)[p->nwaiters++] = t;
      yield_to_scheduler(t);
    }
    return p->value;
  }

  // On the main thread: pump the loop until the promise settles.
  while (p->state == 0) {
    if (!loop_step()) break; // nothing left to run → would deadlock
  }
  return p->value;
}

ObjPromise *aot_sleep_async(long long ms) {
  ensure_scheduler_inited();
  ObjPromise *p = aot_promise_new();
  Timer tm;
  tm.deadline_ms = now_ms() + (ms < 0 ? 0 : ms);
  tm.promise = p;
  g_timers.push_back(tm);
  return p;
}

void aot_event_loop_run(void) {
  ensure_scheduler_inited();
  while (loop_step()) { /* drain */ }
}

} // extern "C"
