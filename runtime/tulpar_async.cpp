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
#include "../src/vm/boxed_call.hpp"
#include "../src/common/localization.hpp"

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
void aot_runtime_error(const char *msg); // src/vm/runtime_bindings.cpp
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
// Kalici (malloc'lu, olumsuz) dizgi — iptal/zaman asimi hata degeri icin.
ObjString *aot_intern_string(const char *chars, int length);
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
  // IPTAL (K112). cancel(p) bayragi kaldirir; gorev bir sonraki `await`
  // noktasinda (ya da hic baslamadiysa baslarken) iptal hatasini firlatir.
  // Kooperatif ve TEK ATIS: firlatinca bayrak iner — hatayi yakalayip devam
  // eden gorev normal biter (asyncio CancelledError ile ayni).
  bool cancelled = false;
  // Gorevin su an bekledigi promise (await icinde, yield'den once yazilir).
  // cancel() gorevi o promise'in bekleyen listesinden cikarip hazir kuyruga
  // koyar ki park etmis bir gorev iptali HEMEN gorsun.
  ObjPromise *waiting_on = nullptr;
  // BAG gorevi (with_timeout): coroutine DEGIL — yigin yok. `link_src`
  // yerine gelince `result`'u ayni sonucla yerine getirir. resume() bunu
  // baglam degistirmeden isler.
  ObjPromise *link_src = nullptr;
};

// ---- Timer ---------------------------------------------------------------
struct Timer {
  long long deadline_ms;
  ObjPromise *promise;
  // 0 = sleep_async: suresi dolunca VOID ile yerine gelir.
  // 1 = with_timeout: suresi dolunca `promise` ZAMAN ASIMI ile reddedilir ve
  //     `target` (sarilan is) iptal edilir.
  int kind = 0;
  ObjPromise *target = nullptr;
};

// ---- Background-I/O source -----------------------------------------------
// A completion callback the loop polls on the main thread (see aot_io_register).
struct IoSource {
  int (*poll)(void *ud);
  void *ud;
};

// ---- Scheduler state -----------------------------------------------------
// THREAD BASINA bir zamanlayici (thread_local). Eskiden surec-global'di: iki
// thread ayni anda async kod kosturunca (thread_create icinde await/gather,
// listen_pool isciisinde async handler) ayni g_ready'yi ve AYNI g_main_ctx'i
// paylasiyorlardi — bir thread'in coroutine'i otekinin zamanlayici baglamina
// swapcontext ile donuyordu. Olculdu (2026-09-27, K265): 4 thread x 50 tur
// gather -> "stack smashing detected" / SIGSEGV, uc kosumun ucu de. Her
// thread kendi olay dongusunu kosuyor; bir thread'de olusan promise baska bir
// thread'de beklenemez (zaten desteklenmiyordu). Arka plan G/C kaynaklari
// kaydedildikleri thread'in dongusunde yoklanir.
//
// Kaplar thread_local NESNE degil, thread_local ISARETCIYLE ulasilan yigin
// nesnesi. Olculen (2026-09-28, Windows CI, MinGW fiber yolu): yikicili bir
// thread_local vector'un ILK erisimi bir coroutine'in (fiber'in) icinde
// oldugunda async paketi uc kosumda uc kez coktu (erisim ihlali + yigin
// tasmasi, longjmp geri sariminda, yiginin disinda); ayni kaplar isaretcinin
// arkasina alininca temiz. Mekanizma (cikarim, olculmedi): MinGW'de
// thread_local yikici kaydi ilk erisimin oldugu fiber'a baglaniyor ve
// DeleteFiber'da kosuyor — kap, thread hala kullanirken yikiliyor. Isaretci
// trivially destructible: kayit yok, yikim yok. Bedeli: async kullanan her
// thread'in birkac bos vector'u thread bitince birakilmiyor. Hiz farki yok
// (spawn+await 119,5 -> 119,5 ns).
struct SchedState {
  std::vector<Task *> ready;        // runnable tasks
  std::vector<Timer> timers;        // pending timers (unsorted; min-scanned)
  std::vector<IoSource> io_sources; // background-I/O completion polls
};
thread_local SchedState *t_sched = nullptr;
inline SchedState &sched() {
  SchedState *st = t_sched;
  if (__builtin_expect(st == nullptr, 0)) st = t_sched = new SchedState();
  return *st;
}
#define g_ready (sched().ready)
#define g_timers (sched().timers)
#define g_io_sources (sched().io_sources)
thread_local Task *g_current = nullptr;          // task currently executing (null on main)

// How often to poll outstanding background I/O when nothing else is runnable.
constexpr long long kIoPollMs = 1;

// Iptal / zaman asimi hata degerleri. Yerelden BAGIMSIZ, iki dilli sabit metin:
// kullanici `contains(toString(e), "iptal")` ya da "cancelled" ile ayirt
// edebilsin, LC_ALL'a gore degismesin. Kalici (intern) dizgi — promise'te
// saklanan deger kare/istek arenasi geri sarilinca olmemeli.
VMValue async_error_value(const char *text) {
  ObjString *s = aot_intern_string(text, (int)strlen(text));
  VMValue v;
  v.type = VM_VAL_OBJ;
  v.as.obj = (Obj *)s;
  return v;
}
VMValue cancel_error() {
  static VMValue v = async_error_value("iptal edildi / cancelled");
  return v;
}
VMValue timeout_error() {
  static VMValue v = async_error_value("zaman asimi / timeout");
  return v;
}

// `t`yi `p`nin bekleyen listesinden cikar. Bulunduysa true.
bool remove_waiter(ObjPromise *p, Task *t) {
  if (!p || !p->waiters) return false;
  Task **w = (Task **)p->waiters;
  for (int i = 0; i < p->nwaiters; i++) {
    if (w[i] == t) {
      for (int j = i + 1; j < p->nwaiters; j++) w[j - 1] = w[j];
      p->nwaiters--;
      return true;
    }
  }
  return false;
}

void add_waiter(ObjPromise *p, Task *t) {
  if (p->nwaiters >= p->cap_waiters) {
    int nc = p->cap_waiters ? p->cap_waiters * 2 : 4;
    p->waiters = realloc(p->waiters, sizeof(Task *) * nc);
    p->cap_waiters = nc;
  }
  ((Task **)p->waiters)[p->nwaiters++] = t;
}

// with_timeout'un zamanlayicisini kaldir (R erken yerine geldi): kalirsa olay
// dongusu program sonunda suresinin dolmasini BEKLERDI.
void drop_timeout_timer(ObjPromise *r) {
  for (size_t i = 0; i < g_timers.size(); i++) {
    if (g_timers[i].kind == 1 && g_timers[i].promise == r) {
      g_timers.erase(g_timers.begin() + i);
      return;
    }
  }
}

int cancel_promise(ObjPromise *p);

#if TULPAR_ASYNC_FIBERS
thread_local void *g_main_fiber = nullptr;    // scheduler fiber (converted from thread)
#else
thread_local ucontext_t g_main_ctx;           // scheduler context
#endif

long long now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

// Invoke a top-level user function via the AOT ABI:
//   void fn(VMValue* ret, VMValue* arg0, VMValue* arg1, ...)
// The switch lives in src/vm/boxed_call.hpp, shared with call() and closures.
// It used to be a hand-written 0..16 copy here whose default branch printed a
// note and returned VOID — the awaiting code got 0 and the process exited 0
// (measured 2026-09-27, 17-param async fn). aot_async_spawn now rejects
// argc > TULPAR_CALL_MAX_ARGS at the call site, so this never sees it.
VMValue call_user_fn(void *fn, VMValue *a, int argc) {
  VMValue r;
  r.type = VM_VAL_VOID;
  r.as.int_val = 0;
  tulpar_boxed_call(fn, &r, a, argc);
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
    if (t->result->task == t) t->result->task = nullptr;
    aot_promise_settle(t->result, aot_get_exception(), /*rejected*/ 2);
    return;
  }
  VMValue rv = t->gather ? gather_body(t->gather)
                         : call_user_fn(t->fn, t->args, t->argc);
  aot_try_pop(); // pop the root frame on the normal (non-throwing) path
  t->done = true;
  if (t->result->task == t) t->result->task = nullptr;
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
// reads on entry. Safe because tasks start one at a time under the scheduler
// (per thread: makecontext'in trambolini baslatan thread'de kosar).
thread_local Task *g_starting = nullptr;
void ctx_trampoline() {
  Task *t = g_starting;
  task_body(t);
  // Falls through to uc_link (g_main_ctx) on return.
}
#endif

#if !TULPAR_ASYNC_FIBERS
// Coroutine yigini havuzu (thread basina). Her async cagri 256 KB'lik bir
// yigin istiyordu (malloc + free); biten gorevin yigini burada bekletilip
// sonraki gorevde yeniden kullaniliyor. Tavanli: patlama sonrasi (binlerce
// eszamanli gorev) fazlasi serbest birakilir, havuz bellegi tutmaz.
// Olculdu 2026-09-28 (Ryzen 7 9800X3D): bkz. CHANGELOG K112 girdisi.
constexpr size_t kStackPoolMax = 32;
// Thread bitince havuzdaki yiginlar da birakilir (thread_create'in isci
// thread'leri biter; havuz onlarla birlikte 8 MB'a kadar sizdirirdi).
struct StackPool {
  std::vector<char *> v;
  ~StackPool() {
    for (char *st : v) free(st);
  }
};
thread_local StackPool g_stack_pool;
char *stack_acquire() {
  if (!g_stack_pool.v.empty()) {
    char *st = g_stack_pool.v.back();
    g_stack_pool.v.pop_back();
    return st;
  }
  return static_cast<char *>(malloc(kCoroStackSize));
}
void stack_release(char *st) {
  if (!st) return;
  if (g_stack_pool.v.size() < kStackPoolMax) g_stack_pool.v.push_back(st);
  else free(st);
}
#endif

// Resume a task: run it until it yields (await) or finishes.
void resume(Task *t) {
  if (t->link_src) {
    // with_timeout bagi: kaynak yerine geldi -> sonucu aktar. Yigin yok.
    ObjPromise *src = t->link_src, *dst = t->result;
    if (dst->task == t) dst->task = nullptr;
    drop_timeout_timer(dst);
    if (dst->state == 0) aot_promise_settle(dst, src->value, src->state);
    delete t;
    return;
  }
  if (t->cancelled && !t->started) {
    // Hic baslamadan iptal: kullanici kodu KOSMAZ, promise reddedilir.
    if (t->result->task == t) t->result->task = nullptr;
    if (t->gather) {
      GatherState *gs = t->gather;
      for (int i = 0; i < gs->n; i++) arc_release_vmvalue(&gs->items[i]);
      free(gs->items);
      delete gs;
    }
    if (t->args) {
      for (int i = 0; i < t->argc; i++) arc_release_vmvalue(&t->args[i]);
      free(t->args);
    }
    aot_promise_settle(t->result, cancel_error(), /*rejected*/ 2);
    delete t;
    return;
  }
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
    t->stack = stack_acquire();
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
    stack_release(t->stack);
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
//
// `drain` = program sonu bosaltmasi (aot_event_loop_run). Orada BEKLEYENI
// OLMAYAN bir sleep_async zamanlayicisi hicbir seyi uyandiramaz: calisacak
// gorev yok, G/C yok, onu bekleyen de yok — suresini beklemek yalniz cikisi
// geciktirir. Bu, iptal (K112) ile somutlasti: iptal edilen gorev
// `sleep_async(1000)`in bekleyen listesinden cikiyor ve program cikisi 1 sn
// bekliyordu (olculdu 2026-09-28). Ana thread'in `await`i ise (drain=false)
// her zamanlayiciyi canli sayar: orada bekleyen ANA thread'dir, gorev degil,
// yani bekleyen listesinde gorunmez.
bool loop_step(bool drain = false) {
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
  if (drain && !has_io) {
    bool live = false;
    for (const Timer &tm : g_timers)
      if (tm.kind != 0 || tm.promise->nwaiters > 0) { live = true; break; }
    if (!live) return false;
  }
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
    std::vector<Timer> fire;
    std::vector<Timer> keep;
    for (auto &tm : g_timers) {
      if (tm.deadline_ms <= t_now) fire.push_back(tm);
      else keep.push_back(tm);
    }
    g_timers.swap(keep);
    VMValue v;
    v.type = VM_VAL_VOID;
    v.as.int_val = 0;
    for (const Timer &tm : fire) {
      if (tm.kind == 1) {
        // with_timeout: sure doldu. Sonucu ZAMAN ASIMI ile reddet, sarilan
        // isi iptal et (asyncio.wait_for gibi — zaman asimina ugrayan is
        // arkada kosmaya devam etmesin).
        if (tm.promise->state == 0) {
          aot_promise_settle(tm.promise, timeout_error(), 2);
          cancel_promise(tm.target);
        }
      } else {
        aot_promise_settle(tm.promise, v, 1);
      }
    }
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

// Promise'i iptal et. Donus: 1 = iptal istendi / uygulandi, 0 = zaten yerine
// gelmis ya da iptal edilecek bir sey yok.
//   * gorev promise'i (async fn, gather): gorev bayraklanir; park etmisse
//     uyandirilir, bir sonraki await'inde iptal hatasini firlatir. gather
//     iptal edilirse cocuklari da iptal edilir (yapisal).
//   * with_timeout sonucu: sarilan kaynak iptal edilir; sonuc onu izler.
//   * gorevsiz promise (sleep_async, async HTTP): dogrudan reddedilir.
int cancel_promise(ObjPromise *p) {
  if (!p || p->state != 0) return 0;
  Task *t = (Task *)p->task;
  if (!t) {
    aot_promise_settle(p, cancel_error(), 2);
    return 1;
  }
  if (t->link_src) return cancel_promise(t->link_src);
  if (t->done) return 0;
  t->cancelled = true;
  if (t->gather) {
    GatherState *gs = t->gather;
    for (int i = 0; i < gs->n; i++)
      if (IS_PROMISE(gs->items[i])) cancel_promise(AS_PROMISE(gs->items[i]));
  }
  if (t != g_current && t->waiting_on && remove_waiter(t->waiting_on, t)) {
    t->waiting_on = nullptr;
    g_ready.push_back(t);
  }
  return 1;
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
  p->task = nullptr;
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
  if (argc > TULPAR_CALL_MAX_ARGS) {
    // Cagri yerinde, senkron: coroutine hic kurulmaz. aot_runtime_error
    // firlatir (yakalanmazsa cikis 1). Donus yalniz TULPAR_SOFT_RUNTIME'da:
    // fonksiyon CAGRILMAZ (eksik isaretciyle cagri cop okumak olurdu), VOID
    // ile yerine gelmis bir promise doner — yumusak modun "tani + 0" kurali.
    char b[256];
    std::snprintf(b, sizeof b, "%s: %d (%s %d)",
                  tulpar::i18n::tr_en(
                      "Calisma Zamani Hatasi: async fonksiyonun parametre sayisi tavani asiyor",
                      "Runtime Error: async function has too many parameters"),
                  argc, tulpar::i18n::tr_en("en fazla", "at most"),
                  TULPAR_CALL_MAX_ARGS);
    aot_runtime_error(b);
    ObjPromise *p = aot_promise_new();
    VMValue v;
    v.type = VM_VAL_VOID;
    v.as.int_val = 0;
    aot_promise_settle(p, v, /*fulfilled*/ 1);
    return p;
  }
  Task *t = new Task();
  t->fn = fn;
  t->argc = argc;
  if (argc > 0) {
    t->args = (VMValue *)malloc(sizeof(VMValue) * argc);
    memcpy(t->args, args, sizeof(VMValue) * argc);
    for (int i = 0; i < argc; i++) arc_retain_vmvalue(&t->args[i]);
  }
  t->result = aot_promise_new();
  t->result->task = t;
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
    Task *t = g_current;
    while (p->state == 0) {
      if (t->cancelled) break;
      add_waiter(p, t);
      t->waiting_on = p;
      yield_to_scheduler(t);
      t->waiting_on = nullptr;
    }
    if (t->cancelled) {
      // Iptal bu await noktasinda gorunur: tek atis — bayrak iner, hata
      // coroutine'in kendi yiginina firlar (kullanici try/catch'i yakalar,
      // yoksa task_body kokunden promise reddedilir).
      t->cancelled = false;
      VMValue ce = cancel_error();
      aot_throw_ptr(&ce);
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
  t->result->task = t;
  g_ready.push_back(t);
  return t->result;
}

void aot_event_loop_run(void) {
  ensure_scheduler_inited();
  while (loop_step(/*drain*/ true)) { /* drain */ }
}

// with_timeout(p, ms) -> promise (K112). `p` `ms` milisaniye icinde yerine
// gelirse onun sonucu; gelmezse ZAMAN ASIMI ile reddedilir ve `p`nin isi iptal
// edilir. Promise olmayan ya da zaten yerine gelmis `p` oldugu gibi doner.
// Sonuca bagli iki sey var: kaynagin bekleyen listesindeki BAG gorevi (yigin
// yok, resume() baglam degistirmeden aktarir) ve zamanlayici (kind 1). Hangisi
// once olursa sonucu belirler; bag erken biterse zamanlayiciyi kaldirir.
VMValue aot_async_with_timeout_ptr(VMValue *pv, VMValue *msv) {
  VMValue none;
  none.type = VM_VAL_VOID;
  none.as.int_val = 0;
  if (!pv) return none;
  VMValue v = *pv;
  if (!IS_PROMISE(v) || AS_PROMISE(v)->state != 0) return v;
  long long ms = 0;
  if (msv && IS_INT(*msv)) {
    ms = AS_INT(*msv);
  } else if (msv && IS_FLOAT(*msv)) {
    ms = (long long)AS_FLOAT(*msv);
  } else {
    aot_runtime_error(tulpar::i18n::tr_en(
        "Calisma Zamani Hatasi: with_timeout() ikinci arguman milisaniye (sayi) bekler",
        "Runtime Error: with_timeout() expects milliseconds (a number) as its second argument"));
    return v;
  }
  ensure_scheduler_inited();
  ObjPromise *src = AS_PROMISE(v);
  ObjPromise *r = aot_promise_new();
  Task *link = new Task();
  link->link_src = src;
  link->result = r;
  r->task = link;
  add_waiter(src, link);
  Timer tm;
  tm.deadline_ms = now_ms() + (ms < 0 ? 0 : ms);
  tm.promise = r;
  tm.kind = 1;
  tm.target = src;
  g_timers.push_back(tm);
  VMValue out;
  out.type = VM_VAL_OBJ;
  out.as.obj = (Obj *)r;
  return out;
}

// cancel(p) -> bool (K112). Bkz. cancel_promise.
VMValue aot_async_cancel_ptr(VMValue *pv) {
  VMValue out;
  out.type = VM_VAL_BOOL;
  out.as.int_val = 0;
  if (pv && IS_PROMISE(*pv)) {
    ensure_scheduler_inited();
    out.as.bool_val = cancel_promise(AS_PROMISE(*pv)) != 0;
  }
  return out;
}

} // extern "C"
