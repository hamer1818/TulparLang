// Tulpar Async Runtime — implementation. See tulpar_async.h for the model.
//
// Stackful coroutines, four context-switch backends:
//   * x86_64 / AArch64 (Linux, macOS, Android): a hand-written switch
//     (tulpar_ctx_swap below) — callee-saved registers + stack pointer only.
//   * Windows: the Fiber API.
//   * Web (Emscripten, wasm32): emscripten_fiber_* — rides on ASYNCIFY, which
//     every web link already enables (aot_pipeline.cpp, raylib's loop).
//   * anything else POSIX: <ucontext.h>.
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
#include <cstdint>
#include <vector>
#include <chrono> // steady_clock only (header-only; safe on all toolchains)

// NOTE: deliberately NOT <thread>/<std::this_thread>. MinGW built with the
// win32 threads model omits std::thread/std::this_thread, and this codebase
// otherwise uses native threads (platform_threads.h), so <thread> here broke
// the Windows (MSYS2) build. We sleep via the OS primitive instead.
#if defined(_WIN32)
#define TULPAR_ASYNC_FIBERS 1
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
// Web (K233): wasm'da yigin isaretcisine el ile dokunulamaz; Emscripten'in
// fiber API'si her coroutine'e bir C yigini + bir ASYNCIFY tamponu verir.
// ASYNCIFY web linkinde zaten acik (raylib'in EndDrawing'i emscripten_sleep
// cagiriyor), yani async'i olmayan oyunlarin ikilisi bundan etkilenmez.
#define TULPAR_ASYNC_EMFIBER 1
#include <emscripten/fiber.h>
#include <unistd.h> // usleep
#elif defined(__x86_64__) || defined(__aarch64__)
// El yazimi baglam gecisi (K233). NEDEN:
//   1. Android: bionic makecontext/swapcontext'i KALDIRDI (NDK r27
//      sysroot'unda yok) — async bu yuzden Android'de hic yoktu.
//   2. Hiz: glibc swapcontext her geciste sinyal maskesini kaydedip yukluyor
//      (rt_sigprocmask SISTEM CAGRISI); bir spawn+await iki gecis + bir
//      getcontext = uc sistem cagrisi. Olculdu (2026-09-28, Ryzen 7 9800X3D):
//      spawn+await ~360 ns'nin cogu buydu.
// Tulpar coroutine'leri sinyal maskesine dokunmuyor; gecis yalniz ABI'nin
// callee-saved yazmaclari + yigin isaretcisi. tulpar-engine'in
// fiber_switch_*.S'i ile ayni kalip.
#define TULPAR_ASYNC_ASM 1
#include <unistd.h> // usleep
// AddressSanitizer yigin degisimini BILMEZ (swapcontext'i yakaliyordu, el
// yazimi gecisi yakalayamaz): bildirilmezse coroutine yiginindaki her
// erisim sahte "stack-buffer-overflow" olur. tests/run_asan.sh ve
// TULPAR_AOT_LINK_FLAGS=-fsanitize=address taramalari bunu kullaniyor.
#if defined(__SANITIZE_ADDRESS__)
#define TULPAR_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TULPAR_ASAN 1
#endif
#endif
#if TULPAR_ASAN
#include <sanitizer/common_interface_defs.h>
#define ASAN_START(fake, bottom, size) __sanitizer_start_switch_fiber((fake), (bottom), (size))
#define ASAN_FINISH(fake, bottom, size) __sanitizer_finish_switch_fiber((fake), (bottom), (size))
#else
#define ASAN_START(fake, bottom, size) ((void)0)
#define ASAN_FINISH(fake, bottom, size) ((void)0)
#endif
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
// Program sonu bosaltmasini kaydet (aot_event_loop_run runtime_bindings'de).
void aot_async_set_drain(void (*fn)(void));
void tulpar_async_drain_all(void); // asagida, extern "C" blogunda

// Async iz (K156): kutulu giris noktasinin kaynak adi + yakalanmayan istisna
// kancasi (src/vm/runtime_bindings.cpp).
const char *aot_func_name_of(void *fn);
void aot_set_uncaught_hook(void (*hook)(VMValue));
// Runtime array allocators (src/vm/runtime_bindings.cpp). The plain
// vm_allocate_array/vm_array_push deref the VM*, so the AOT runtime (no VM)
// must go through these null-safe wrappers, which malloc when vm == nullptr.
ObjArray *vm_allocate_array_aot_wrapper(void *vm);
void vm_array_push_aot_wrapper(void *vm, ObjArray *array, VMValue value);
}

#if TULPAR_ASYNC_ASM
// ---------------------------------------------------------------------------
// tulpar_ctx_swap(void **save_sp, void *load_sp)
//   Cagiranin callee-saved yazmaclarini KENDI yiginina iter, yigin
//   isaretcisini *save_sp'ye yazar, load_sp'deki cerceveyi yukleyip oradan
//   `ret` eder. Ilk kez girilen coroutine icin cerceveyi ctx_init_stack kurar:
//   `ret` tulpar_ctx_entry'ye duser, o da coro_main(Task*)'i cagirir.
//
// x86_64 SysV: rbx rbp r12-r15 + MXCSR/x87 kontrol sozcugu (ABI callee-saved
//   sayar). Cerceve (dusuk adresten): [mxcsr,fpucw][r15][r14][r13][r12][rbx]
//   [rbp][donus] = 64 bayt.
// AArch64 AAPCS64 (Linux, Android, Apple): x19-x28, x29(fp), x30(lr),
//   d8-d15 = 160 bayt. x18'e (Apple'da platform yazmaci) dokunulmaz.
#if defined(__APPLE__)
#define TULPAR_ASM_SYM(x) "_" #x
#define TULPAR_ASM_FN(x) ".globl _" #x "\n.p2align 4\n_" #x ":\n"
#else
#define TULPAR_ASM_SYM(x) #x
#define TULPAR_ASM_FN(x) ".globl " #x "\n.hidden " #x "\n.type " #x ", %function\n.p2align 4\n" #x ":\n"
#endif
extern "C" void tulpar_ctx_swap(void **save_sp, void *load_sp);
extern "C" void tulpar_ctx_entry(void);
#if defined(__x86_64__)
__asm__(".text\n" TULPAR_ASM_FN(tulpar_ctx_swap)
        "  pushq %rbp\n"
        "  pushq %rbx\n"
        "  pushq %r12\n"
        "  pushq %r13\n"
        "  pushq %r14\n"
        "  pushq %r15\n"
        "  subq $8, %rsp\n"
        "  stmxcsr (%rsp)\n"
        "  fnstcw 4(%rsp)\n"
        "  movq %rsp, (%rdi)\n"
        "  movq %rsi, %rsp\n"
        "  ldmxcsr (%rsp)\n"
        "  fldcw 4(%rsp)\n"
        "  addq $8, %rsp\n"
        "  popq %r15\n"
        "  popq %r14\n"
        "  popq %r13\n"
        "  popq %r12\n"
        "  popq %rbx\n"
        "  popq %rbp\n"
        "  ret\n"
        TULPAR_ASM_FN(tulpar_ctx_entry)
        "  movq %r12, %rdi\n"   // Task*
        "  callq *%r13\n"       // coro_main(Task*) — donmez
        "  ud2\n");
#else // __aarch64__
__asm__(".text\n" TULPAR_ASM_FN(tulpar_ctx_swap)
        "  sub sp, sp, #160\n"
        "  stp x19, x20, [sp, #0]\n"
        "  stp x21, x22, [sp, #16]\n"
        "  stp x23, x24, [sp, #32]\n"
        "  stp x25, x26, [sp, #48]\n"
        "  stp x27, x28, [sp, #64]\n"
        "  stp x29, x30, [sp, #80]\n"
        "  stp d8, d9, [sp, #96]\n"
        "  stp d10, d11, [sp, #112]\n"
        "  stp d12, d13, [sp, #128]\n"
        "  stp d14, d15, [sp, #144]\n"
        "  mov x9, sp\n"
        "  str x9, [x0]\n"
        "  mov sp, x1\n"
        "  ldp x19, x20, [sp, #0]\n"
        "  ldp x21, x22, [sp, #16]\n"
        "  ldp x23, x24, [sp, #32]\n"
        "  ldp x25, x26, [sp, #48]\n"
        "  ldp x27, x28, [sp, #64]\n"
        "  ldp x29, x30, [sp, #80]\n"
        "  ldp d8, d9, [sp, #96]\n"
        "  ldp d10, d11, [sp, #112]\n"
        "  ldp d12, d13, [sp, #128]\n"
        "  ldp d14, d15, [sp, #144]\n"
        "  add sp, sp, #160\n"
        "  ret\n"
        TULPAR_ASM_FN(tulpar_ctx_entry)
        "  mov x0, x19\n"       // Task*
        "  blr x20\n"           // coro_main(Task*) — donmez
        "  brk #0\n");
#endif

namespace {
// Yeni coroutine yigininin ustune, tulpar_ctx_swap'in "geri yukleyecegi"
// ilk cerceveyi kur. Donus: coroutine'in kaydedilmis yigin isaretcisi.
void *ctx_init_stack(char *stack, size_t size, void *task, void *fn) {
  // Tepe 16'ya hizali, 16 bayt pay birakilir.
  uintptr_t top = ((uintptr_t)(stack + size) & ~(uintptr_t)15) - 16;
#if defined(__x86_64__)
  // `ret` sonrasi rsp = top (16'ya hizali): entry'deki `call` ABI'ye uygun.
  uint64_t *f = (uint64_t *)(top - 64);
  uint32_t csr[2] = {0x1F80u /* mxcsr varsayilan */, 0x037Fu /* x87 cw */};
  memcpy(&f[0], csr, 8);
  f[1] = 0;                  // r15
  f[2] = 0;                  // r14
  f[3] = (uint64_t)fn;       // r13 -> coro_main
  f[4] = (uint64_t)task;     // r12 -> Task*
  f[5] = 0;                  // rbx
  f[6] = 0;                  // rbp
  f[7] = (uint64_t)&tulpar_ctx_entry; // ret hedefi
  return f;
#else
  uint64_t *f = (uint64_t *)(top - 160);
  memset(f, 0, 160);
  f[0] = (uint64_t)task;     // x19 -> Task*
  f[1] = (uint64_t)fn;       // x20 -> coro_main
  f[10] = 0;                 // x29 (fp) = 0: geri izleme burada durur
  f[11] = (uint64_t)&tulpar_ctx_entry; // x30 (lr) -> ret hedefi
  return f;
#endif
}
} // namespace
#endif // TULPAR_ASYNC_ASM

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
#elif TULPAR_ASYNC_EMFIBER
  emscripten_fiber_t fib;     // C yigini + ASYNCIFY tamponu tanimi
  char *stack = nullptr;      // C yigini (havuzdan)
  char *astack = nullptr;     // ASYNCIFY tamponu (askida canli wasm yerelleri)
#elif TULPAR_ASYNC_ASM
  void *sp = nullptr;     // kaydedilmis yigin isaretcisi (askidayken)
  char *stack = nullptr;
  void *asan_fake = nullptr; // ASan sahte yigin tutamaci (yalniz ASan'da)
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
  // Bu gorevde await'in en son yeniden firlattigi red (async iz): kok catch
  // ayni degeri yakalarsa "bu gorev o promise'in reddini iletti" demektir.
  ObjPromise *rethrow_src = nullptr;
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
  std::vector<ObjPromise *> rejected; // koken kaydi olan redler (async iz, K156)
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
#define g_rejected (sched().rejected)
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

// ---- Async iz (K156) -------------------------------------------------------
// Senkron kodda yakalanmayan hata en azindan mesajini basiyordu; async'te
// hatanin HANGI gorevde dogdugu ve hangi await'lerden gectigi kayboluyordu:
// ic ice uc async fonksiyonda "Uncaught Exception: boom" ve baska hicbir sey.
// Ustelik hic await edilmeyen gorevin hatasi SESSIZCE yutuluyordu (cikis 0).
// Promise reddedilince kokeni kaydedilir; yakalanmayan hatada zincir, program
// sonunda da hic gozlenmemis redler stderr'e basilir.
struct AsyncOrigin {
  const char *name;   // reddeden gorevin fonksiyonu ("gather" / "?")
  ObjPromise *cause;  // bu gorev baska bir promise'in reddini await'te ilettiyse
  bool observed;      // biri await etti / bagla iletildi
};
thread_local ObjPromise *g_main_rethrow_src = nullptr; // ana thread'in son iletimi

bool same_value(VMValue a, VMValue b) {
  return a.type == b.type && memcmp(&a.as, &b.as, sizeof a.as) == 0;
}

void mark_observed(ObjPromise *p) {
  if (p && p->origin) static_cast<AsyncOrigin *>(p->origin)->observed = true;
}

// Reddedilen gorev promise'ine koken yaz (task_body'nin kok catch'i).
void record_origin(ObjPromise *p, const char *name, VMValue exc, ObjPromise *rethrow_src) {
  AsyncOrigin *o = static_cast<AsyncOrigin *>(malloc(sizeof(AsyncOrigin)));
  if (!o) return;
  o->name = name ? name : "?";
  o->cause = (rethrow_src && rethrow_src->state == 2 &&
              same_value(rethrow_src->value, exc)) ? rethrow_src : nullptr;
  o->observed = false;
  p->origin = o;
  g_rejected.push_back(p);
}

void print_chain(ObjPromise *p) {
  // p: ana thread'in await'te yeniden firlattigi promise. Zincir en distan
  // (ana thread'in bekledigi gorev) en ice (hatanin dogdugu gorev) dogru.
  int n = 0;
  for (ObjPromise *q = p; q && q->origin && n < 64; n++)
    q = static_cast<AsyncOrigin *>(q->origin)->cause;
  if (n == 0) return;
  std::fprintf(stderr, "%s\n", tulpar::i18n::tr_en(
      "  async iz (await zinciri, en distaki once):",
      "  async trace (await chain, outermost first):"));
  int i = 0;
  for (ObjPromise *q = p; q && q->origin && i < 64; i++) {
    AsyncOrigin *o = static_cast<AsyncOrigin *>(q->origin);
    const bool last = !o->cause || !o->cause->origin;
    std::fprintf(stderr, "    %s %s  %s\n", i == 0 ? "at" : "  ", o->name,
                 last ? tulpar::i18n::tr_en("<- hata burada firlatildi",
                                            "<- thrown here")
                      : tulpar::i18n::tr_en("(await ile iletti)",
                                            "(re-raised at await)"));
    q = o->cause;
  }
}

void async_uncaught_hook(VMValue exc) {
  ObjPromise *p = g_main_rethrow_src;
  if (p && p->state == 2 && same_value(p->value, exc)) print_chain(p);
}

// Program sonu: hic gozlenmemis redler (await edilmemis gorevin hatasi).
// Iptal edilen gorevler sayilmaz (asyncio da uyarmiyor). Cikis kodu
// DEGISMEZ — yalniz stderr.
void report_unobserved() {
  for (ObjPromise *p : g_rejected) {
    AsyncOrigin *o = static_cast<AsyncOrigin *>(p->origin);
    if (!o || o->observed) continue;
    if (same_value(p->value, cancel_error())) continue;
    o->observed = true; // bir kez
    std::fprintf(stderr, "%s %s: ", tulpar::i18n::tr_en(
                     "Uyari: hic await edilmeyen async gorevin hatasi yutuldu —",
                     "Warning: unhandled error in an async task that was never awaited —"),
                 o->name);
    if (IS_STRING(p->value))
      std::fprintf(stderr, "%s\n", AS_STRING(p->value)->chars);
    else
      std::fprintf(stderr, "<value type=%d>\n", p->value.type);
  }
  g_rejected.clear();
}

#if TULPAR_ASYNC_FIBERS
thread_local void *g_main_fiber = nullptr;    // scheduler fiber (converted from thread)
#elif TULPAR_ASYNC_EMFIBER
// ASYNCIFY tamponu: askidaki baglamin canli wasm yerelleri buraya yazilir.
// Tulpar fonksiyonlari giris-yukseltilmis VMValue alloca'lariyla dolu; link
// satirindaki ASYNCIFY_STACK_SIZE (128 KB, aot_pipeline.cpp) ile ayni gerekce.
constexpr size_t kAsyncifyStackSize = 128 * 1024;
thread_local emscripten_fiber_t g_main_fib;   // zamanlayicinin (ana) baglami
thread_local char *g_main_astack = nullptr;
#elif TULPAR_ASYNC_ASM
thread_local void *g_main_sp = nullptr;       // zamanlayicinin kaydedilmis yigini
#if TULPAR_ASAN
thread_local const void *g_main_stack_bottom = nullptr; // ASan: zamanlayici yigini
thread_local size_t g_main_stack_size = 0;
#endif
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
    VMValue exc = aot_get_exception();
    const char *nm = t->fn ? aot_func_name_of(t->fn) : "gather";
    record_origin(t->result, nm, exc, t->rethrow_src);
    aot_promise_settle(t->result, exc, /*rejected*/ 2);
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
#elif TULPAR_ASYNC_EMFIBER
// Fiber girisi: donmemeli (Emscripten'de tanimsiz) — gorev bitince
// zamanlayiciya gecer, bu fiber'a bir daha girilmez.
void em_coro_main(void *arg) {
  Task *t = static_cast<Task *>(arg);
  task_body(t);
  emscripten_fiber_swap(&t->fib, &g_main_fib);
  __builtin_trap(); // ulasilmaz
}
#elif TULPAR_ASYNC_ASM
// Yeni coroutine'in ilk isi (tulpar_ctx_entry buraya Task* ile atlar).
// Donmez: gorev bitince zamanlayiciya gecer ve bu yigina bir daha girilmez
// (resume() gorevi siler, yigini havuza verir).
void coro_main(Task *t) {
  ASAN_FINISH(nullptr, &g_main_stack_bottom, &g_main_stack_size);
  task_body(t);
  ASAN_START(nullptr /* bu yigin oluyor */, g_main_stack_bottom, g_main_stack_size);
  tulpar_ctx_swap(&t->sp, g_main_sp);
  __builtin_trap(); // ulasilmaz
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
    mark_observed(src); // red bagla iletildi: artik dst'nin sorunu
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
#elif TULPAR_ASYNC_EMFIBER
  if (!t->started) {
    t->started = true;
    t->stack = stack_acquire();
    t->astack = static_cast<char *>(malloc(kAsyncifyStackSize));
    emscripten_fiber_init(&t->fib, em_coro_main, t, t->stack, kCoroStackSize,
                          t->astack, kAsyncifyStackSize);
  }
  emscripten_fiber_swap(&g_main_fib, &t->fib);
#elif TULPAR_ASYNC_ASM
  if (!t->started) {
    t->started = true;
    t->stack = stack_acquire();
    t->sp = ctx_init_stack(t->stack, kCoroStackSize, t, (void *)&coro_main);
  }
  {
    void *main_fake = nullptr;
    (void)main_fake;
    ASAN_START(&main_fake, t->stack, kCoroStackSize);
    tulpar_ctx_swap(&g_main_sp, t->sp);
    ASAN_FINISH(main_fake, nullptr, nullptr);
  }
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
#if TULPAR_ASYNC_EMFIBER
    free(t->astack);
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
#elif TULPAR_ASYNC_EMFIBER
  emscripten_fiber_swap(&t->fib, &g_main_fib);
#elif TULPAR_ASYNC_ASM
  ASAN_START(&t->asan_fake, g_main_stack_bottom, g_main_stack_size);
  tulpar_ctx_swap(&t->sp, g_main_sp);
  ASAN_FINISH(t->asan_fake, &g_main_stack_bottom, &g_main_stack_size);
#else
  swapcontext(&t->ctx, &g_main_ctx);
#endif
}

void ensure_scheduler_inited() {
  // Surec basina bir kez; sihirli static thread-guvenli (isciler de async
  // kullanabilir). Sonraki cagrilarda yalniz bir guard okumasi.
  static const bool drain_set = (aot_async_set_drain(tulpar_async_drain_all), true);
  (void)drain_set;
#if TULPAR_ASYNC_EMFIBER
  if (!g_main_astack) {
    g_main_astack = static_cast<char *>(malloc(kAsyncifyStackSize));
    emscripten_fiber_init_from_current_context(&g_main_fib, g_main_astack,
                                               kAsyncifyStackSize);
  }
#endif

  static const bool hook_set = (aot_set_uncaught_hook(async_uncaught_hook), true);
  (void)hook_set;
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
  p->origin = nullptr;
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
    if (p->state == 2) {
      mark_observed(p);
      t->rethrow_src = p;
      aot_throw_ptr(&p->value);
    }
    return p->value;
  }

  // On the main thread: pump the loop until the promise settles.
  while (p->state == 0) {
    if (!loop_step()) break; // nothing left to run → would deadlock
  }
  // Rejection on the main thread surfaces as a throw (caught by a top-level
  // try/catch, else the uncaught-exception handler exits — and prints the
  // await chain through async_uncaught_hook).
  if (p->state == 2) {
    mark_observed(p);
    g_main_rethrow_src = p;
    aot_throw_ptr(&p->value);
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

// aot_event_loop_run'in govdesi. aot_event_loop_run'in KENDISI
// src/vm/runtime_bindings.cpp'de: main() onu KOSULSUZ cagiriyor ve burada
// tanimli oldugu surece bu nesne (ve web'de Emscripten'in fiber JS'i)
// async kullanmayan her ikiliye giriyordu — olculdu (2026-09-28): web oyunu
// arcade_zipla .wasm +38 KB, .js +15,6 KB. Zamanlayici ilk kullanimda bu
// fonksiyonu kaydeder (aot_async_set_drain); kullanilmadiysa cagri no-op.
void tulpar_async_drain_all() {
  if (!t_sched) return; // bu thread'de async hic kullanilmadi
  ensure_scheduler_inited();
  while (loop_step(/*drain*/ true)) { /* drain */ }
  report_unobserved();
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
