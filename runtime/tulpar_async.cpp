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
#include <csetjmp>
#include <vector>
#include <chrono> // steady_clock only (header-only; safe on all toolchains)

// NOTE: deliberately NOT <thread>/<std::this_thread>. MinGW built with the
// win32 threads model omits std::thread/std::this_thread, and this codebase
// otherwise uses native threads (platform_threads.h), so <thread> here broke
// the Windows (MSYS2) build. We sleep via the OS primitive instead.
#if defined(_WIN32)
#define TULPAR_ASYNC_FIBERS 1
#include <windows.h>
#else
#include <ucontext.h>
#include <unistd.h> // usleep
#endif

namespace {
inline void async_sleep_ms(long long ms) {
  if (ms <= 0) return;
#if defined(_WIN32)
  Sleep((DWORD)ms);
#else
  usleep((useconds_t)(ms * 1000));
#endif
}
} // namespace

extern "C" {
void arc_retain_vmvalue(VMValue *val);
void arc_release_vmvalue(VMValue *val);
// Exception-handler runtime (src/vm/runtime_bindings.cpp). Coroutines get their
// own handler context so an uncaught throw rejects the promise instead of
// longjmp'ing across stacks; await re-raises a rejection in the awaiter.
jmp_buf *aot_try_push(void);
void aot_try_pop(void);
void aot_throw_ptr(VMValue *exception_ptr);
VMValue aot_get_exception(void);
void *aot_eh_context_new(void);
void aot_eh_context_free(void *ctx);
void *aot_eh_context_swap(void *ctx);
// Runtime array allocators (src/vm/runtime_bindings.cpp). The plain
// vm_allocate_array/vm_array_push deref the VM*, so the AOT runtime (no VM)
// must go through these null-safe wrappers, which malloc when vm == nullptr.
ObjArray *vm_allocate_array_aot_wrapper(void *vm);
void vm_array_push_aot_wrapper(void *vm, ObjArray *array, VMValue value);
}

namespace {

constexpr size_t kCoroStackSize = 256 * 1024;

// gather() bookkeeping carried by a coroutine that awaits N children.
struct GatherState {
  VMValue *items = nullptr; // retained copies of the awaited args
  int n = 0;
  ObjArray *arr = nullptr;  // partial result array (for reject-path cleanup)
};

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
  GatherState *gather = nullptr; // non-null => this is a gather() coroutine
  ObjPromise *result = nullptr;  // promise fulfilled on return
  void *eh_ctx = nullptr;        // this coroutine's exception-handler context
  bool done = false;
  bool started = false;
};

// ---- Timer ---------------------------------------------------------------
struct Timer {
  long long deadline_ms;
  ObjPromise *promise;
};

// ---- Background-I/O source -----------------------------------------------
// A completion callback the loop polls on the main thread (see aot_io_register).
struct IoSource {
  int (*poll)(void *ud);
  void *ud;
};

// ---- Scheduler state -----------------------------------------------------
std::vector<Task *> g_ready;        // runnable tasks
std::vector<Timer> g_timers;        // pending timers (unsorted; min-scanned)
std::vector<IoSource> g_io_sources; // background-I/O completion polls
Task *g_current = nullptr;          // task currently executing (null on main)

// How often to poll outstanding background I/O when nothing else is runnable.
constexpr long long kIoPollMs = 1;

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
    case 9: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8]); break;
    case 10: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9]); break;
    case 11: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10]); break;
    case 12: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10], &a[11]); break;
    case 13: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10], &a[11], &a[12]); break;
    case 14: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10], &a[11], &a[12], &a[13]); break;
    case 15: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10], &a[11], &a[12], &a[13], &a[14]); break;
    case 16: ((void (*)(VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *, VMValue *))fn)(&r, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10], &a[11], &a[12], &a[13], &a[14], &a[15]); break;
    default:
      fprintf(stderr, "Tulpar async: 16'dan fazla parametreli async fonksiyon desteklenmiyor / async functions with >16 params unsupported\n");
      break;
  }
  return r;
}

// The body of a gather() coroutine: await each child in turn (they progress
// concurrently in the loop) and collect the results into a fresh array.
VMValue gather_body(GatherState *gs) {
  ObjArray *arr = vm_allocate_array_aot_wrapper(nullptr);
  // Publish the array on the state so that if a child rejects (aot_await throws
  // and longjmps out of this frame), task_body's catch can still free it.
  gs->arr = arr;
  for (int i = 0; i < gs->n; i++) {
    VMValue v = aot_await(gs->items[i]);
    vm_array_push_aot_wrapper(nullptr, arr, v);
  }
  for (int i = 0; i < gs->n; i++) arc_release_vmvalue(&gs->items[i]);
  free(gs->items);
  delete gs;
  VMValue out;
  out.type = VM_VAL_OBJ;
  out.as.obj = (Obj *)arr;
  return out;
}

// The body every coroutine runs: call the user function, fulfil the promise.
// A root try-frame catches any throw the user code didn't handle and settles
// the promise *rejected* (state 2) instead of letting longjmp escape the
// coroutine. The coroutine's own EH context is already active here (resume()
// swapped it in), so this frame and any nested user try/catch share it.
void task_body(Task *t) {
  jmp_buf *root = aot_try_push();
  if (root && setjmp(*root) != 0) {
    // An uncaught throw unwound to here — reject with the thrown value.
    // gather_body frees its state on the normal path; on this reject path the
    // throw skipped that, so do the equivalent cleanup (release the retained
    // child args and the partial result array, free the state).
    if (t->gather) {
      GatherState *gs = t->gather;
      for (int i = 0; i < gs->n; i++) arc_release_vmvalue(&gs->items[i]);
      if (gs->arr) {
        VMValue av;
        av.type = VM_VAL_OBJ;
        av.as.obj = (Obj *)gs->arr;
        arc_release_vmvalue(&av); // drops gather's own ref → frees the array
      }
      free(gs->items);
      delete gs;
      t->gather = nullptr;
    }
    t->done = true;
    aot_promise_settle(t->result, aot_get_exception(), /*rejected*/ 2);
    return;
  }
  VMValue rv = t->gather ? gather_body(t->gather)
                         : call_user_fn(t->fn, t->args, t->argc);
  aot_try_pop(); // pop the root frame on the normal (non-throwing) path
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
  // Install this coroutine's exception-handler context for the duration of the
  // slice, restoring the caller's (scheduler / outer coroutine) afterwards so
  // throws stay isolated to the stack they belong to.
  if (!t->eh_ctx) t->eh_ctx = aot_eh_context_new();
  void *prev_eh = aot_eh_context_swap(t->eh_ctx);
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
  aot_eh_context_swap(prev_eh);
  g_current = nullptr;
  if (t->done) {
#if TULPAR_ASYNC_FIBERS
    if (t->fiber) DeleteFiber(t->fiber);
#else
    free(t->stack);
#endif
    if (t->args) free(t->args);
    if (t->eh_ctx) aot_eh_context_free(t->eh_ctx);
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

// Poll every registered background-I/O source once; drop the ones that report
// done (their callback settles its own promise, which may move waiters onto
// g_ready). Returns true if at least one source finished this tick.
bool poll_io_sources() {
  bool any = false;
  for (size_t i = 0; i < g_io_sources.size();) {
    if (g_io_sources[i].poll(g_io_sources[i].ud)) {
      g_io_sources.erase(g_io_sources.begin() + i);
      any = true;
    } else {
      i++;
    }
  }
  return any;
}

// Run one scheduler step. Returns false when there is nothing left to do.
bool loop_step() {
  // Settle any background I/O that finished since the last tick first; this may
  // queue ready tasks (waiters of the settled promise).
  bool io_done = poll_io_sources();

  if (!g_ready.empty()) {
    Task *t = g_ready.front();
    g_ready.erase(g_ready.begin());
    resume(t);
    return true;
  }
  if (io_done) return true;

  bool has_io = !g_io_sources.empty();
  if (!g_timers.empty()) {
    // Find the earliest deadline, sleep until it, fire all that are due. While
    // background I/O is outstanding, cap the wait so we keep polling it.
    size_t mn = 0;
    for (size_t i = 1; i < g_timers.size(); i++)
      if (g_timers[i].deadline_ms < g_timers[mn].deadline_ms) mn = i;
    long long wait = g_timers[mn].deadline_ms - now_ms();
    if (has_io && wait > kIoPollMs) wait = kIoPollMs;
    if (wait > 0)
      async_sleep_ms(wait);
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
  if (has_io) {
    // No tasks, no timers, but a worker thread is still resolving I/O — poll at
    // a fine cadence until it completes.
    async_sleep_ms(kIoPollMs);
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

void aot_io_register(int (*poll)(void *ud), void *ud) {
  ensure_scheduler_inited();
  IoSource s;
  s.poll = poll;
  s.ud = ud;
  g_io_sources.push_back(s);
}

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
    // A rejected promise re-raises in the awaiting coroutine (caught by a user
    // try/catch on its stack, or its task_body root → rejects its own promise).
    if (p->state == 2) aot_throw_ptr(&p->value);
    return p->value;
  }

  // On the main thread: pump the loop until the promise settles.
  while (p->state == 0) {
    if (!loop_step()) break; // nothing left to run → would deadlock
  }
  // Rejection on the main thread surfaces as a throw (caught by a top-level
  // try/catch, else the uncaught-exception handler exits).
  if (p->state == 2) aot_throw_ptr(&p->value);
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

ObjPromise *aot_gather(VMValue *args, int argc) {
  ensure_scheduler_inited();
  GatherState *gs = new GatherState();
  gs->n = argc;
  if (argc > 0) {
    gs->items = (VMValue *)malloc(sizeof(VMValue) * argc);
    memcpy(gs->items, args, sizeof(VMValue) * argc);
    for (int i = 0; i < argc; i++) arc_retain_vmvalue(&gs->items[i]);
  }
  Task *t = new Task();
  t->gather = gs;
  t->result = aot_promise_new();
  g_ready.push_back(t);
  return t->result;
}

void aot_event_loop_run(void) {
  ensure_scheduler_inited();
  while (loop_step()) { /* drain */ }
}

} // extern "C"
