#include "../../runtime/cJSON.h"
#include "../lexer/lexer.hpp"
#include "../common/localization.hpp"
#include "../common/platform_dl.h"
#include "../common/platform.h"
#include "../common/platform_sockets.h"
#include "../common/platform_threads.h"
#include "../pkg/sha256.hpp"
#include "vm.hpp"

// Windows MSVC compatibility: ssize_t is not standard on Windows
#if defined(_MSC_VER) && !defined(ssize_t)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#define TYPE_BOOL_BOOL ((VM_VAL_BOOL << 4) | VM_VAL_BOOL)
#include <cctype>
#include <ctime>
#include <locale.h>
#include <setjmp.h>
#include <stdbool.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <regex>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>

// EXTERN "C" BLOCK - AOT Runtime Functions (called from LLVM compiled code)
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Exception Handling Globals (Removed duplicates)

// ============================================================
// AOT STRING ARENA - Separate high-performance allocator for AOT mode
// Pre-allocates large blocks, avoids malloc overhead
// ============================================================

#define AOT_ARENA_BLOCK_SIZE (1024 * 1024) // 1MB blocks
#define AOT_ARENA_ALIGNMENT 8

typedef struct AOTArenaBlock {
  char *memory;
  size_t size;
  size_t used;
  struct AOTArenaBlock *next;
} AOTArenaBlock;

typedef struct {
  AOTArenaBlock *current;
  AOTArenaBlock *head;
  size_t total_allocated;
} AOTArena;

// Per-thread arena. Originally a single global — fine for the
// historical single-thread Tulpar runtime, but `thread_create()` user
// code (and the upcoming worker-pool Wings) needs per-worker arenas
// so concurrent allocs don't corrupt the bump pointer.
//
// `thread_local` is C++11; works on both gcc/MinGW and MSVC. Each
// worker thread initialises its own arena lazily on first alloc; the
// `aot_arena_init` path is idempotent per thread.
static thread_local AOTArena *g_aot_string_arena = nullptr;

// ---------------------------------------------------------------------------
// Dynamic-call dlsym cache.
//
// HTTP handlers go through `call(handler_name)` once per request, which
// previously meant a `tulpar_dlsym` walk of the whole process symbol
// table on every hit — a significant slice of the time budget on a
// keep-alive server doing 20k+ req/sec. Cache the {name -> fn_ptr}
// mapping so the hot path is O(1) hash lookup + an indirect call.
// 256 slots, linear probing; handler names are tiny (<32 chars), so a
// FNV-1a hash + memcmp is plenty.
// ---------------------------------------------------------------------------
typedef struct {
  // `key` is the publish gate: it is written LAST (release) and read FIRST
  // (acquire) so a lock-free lookup never sees a slot whose klen/khash/ptr are
  // still stale. std::atomic makes that ordering correct on weakly-ordered
  // ARM/aarch64 (Apple Silicon, the aarch64 CMake target) too, not just x86 TSO.
  std::atomic<const char *> key{nullptr}; // strdup'd (small leak on shutdown)
  size_t klen;
  uint32_t khash;
  void (*ptr)(VMValue *);
} AOTCallCacheEntry;

#define AOT_CALL_CACHE_SLOTS 256
static AOTCallCacheEntry g_call_cache[AOT_CALL_CACHE_SLOTS];
// Worker-pool servers can have several threads racing into
// `aot_call_dynamic` for the same handler name. A spinlock on insert
// is enough — the slow path (hash insert with strdup) is rare; lookups
// stay lock-free since the table is append-only and we only ever
// expose populated slots once their `key` field is non-null.
static std::mutex g_call_cache_mu;

static inline uint32_t aot_call_hash(const char *s, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++) {
    h ^= (unsigned char)s[i];
    h *= 16777619u;
  }
  return h;
}

static void (*aot_call_cache_lookup(const char *name, size_t nlen,
                                     uint32_t hash))(VMValue *) {
  uint32_t i = hash & (AOT_CALL_CACHE_SLOTS - 1);
  for (uint32_t step = 0; step < AOT_CALL_CACHE_SLOTS; step++) {
    AOTCallCacheEntry *e = &g_call_cache[(i + step) & (AOT_CALL_CACHE_SLOTS - 1)];
    // Acquire-load the publish gate: if non-null, the matching klen/khash/ptr
    // writes that preceded the release-store in insert are guaranteed visible.
    const char *k = e->key.load(std::memory_order_acquire);
    if (!k) return nullptr;
    if (e->khash == hash && e->klen == nlen && memcmp(k, name, nlen) == 0) {
      return e->ptr;
    }
  }
  return nullptr;
}

static void aot_call_cache_insert(const char *name, size_t nlen, uint32_t hash,
                                  void (*ptr)(VMValue *)) {
  std::lock_guard<std::mutex> guard(g_call_cache_mu);
  uint32_t i = hash & (AOT_CALL_CACHE_SLOTS - 1);
  for (uint32_t step = 0; step < AOT_CALL_CACHE_SLOTS; step++) {
    AOTCallCacheEntry *e = &g_call_cache[(i + step) & (AOT_CALL_CACHE_SLOTS - 1)];
    // Re-check inside the lock — another thread may have just inserted
    // the same key while we were waiting on the mutex. Inserts are serialized
    // by the mutex, so a relaxed load of our own table is fine here.
    const char *k = e->key.load(std::memory_order_relaxed);
    if (e->khash == hash && e->klen == nlen && k &&
        memcmp(k, name, nlen) == 0) {
      return;
    }
    if (!k) {
      char *copy = (char *)malloc(nlen + 1);
      if (!copy) return;
      memcpy(copy, name, nlen);
      copy[nlen] = '\0';
      e->klen = nlen;
      e->khash = hash;
      e->ptr = ptr;
      // Publish the key last with a release store so a concurrent acquire-load
      // in aot_call_cache_lookup sees the klen/khash/ptr writes above. Correct
      // on weakly-ordered ARM/aarch64, not just x86 TSO.
      e->key.store(copy, std::memory_order_release);
      return;
    }
  }
  // Table full — silently drop. With 256 slots and typical handler counts
  // (<50) this never trips.
}

// AOT Dynamic Call Support
VMValue aot_call_dynamic(VMValue func_name) {
  if (!IS_STRING(func_name)) {
    printf("%s\n", tulpar::i18n::tr_en("Calisma Zamani Hatasi: call() string bekler",
                                       "Runtime Error: call() expects string"));
    return VM_VOID();
  }

  ObjString *str = AS_STRING(func_name);
  char *original_name = str->chars;
  size_t orig_len = (size_t)str->length;

  // Hash the original name; cache key is the unprefixed form so we don't
  // need to allocate the prefixed buffer on every cache hit.
  uint32_t hash = aot_call_hash(original_name, orig_len);
  void (*func_ptr)(VMValue *) = aot_call_cache_lookup(original_name, orig_len, hash);

  if (!func_ptr) {
    char name[256];
    if (strcmp(original_name, "main") == 0) {
      snprintf(name, sizeof(name), "main");
    } else {
      snprintf(name, sizeof(name), "t_%s", original_name);
    }
    func_ptr = (void (*)(VMValue *))tulpar_dlsym(TULPAR_RTLD_DEFAULT, name);
    if (!func_ptr) {
      func_ptr = (void (*)(VMValue *))tulpar_dlsym(TULPAR_RTLD_DEFAULT, original_name);
    }
    if (!func_ptr) {
      printf("%s '%s' (AOT)\n",
             tulpar::i18n::tr_en("Calisma Zamani Hatasi: Fonksiyon bulunamadi",
                                 "Runtime Error: Function not found"),
             name);
      const char *error = tulpar_dlerror();
      if (error) {
        printf("  Detail: %s\n", error);
      }
      return VM_VOID();
    }
    aot_call_cache_insert(original_name, orig_len, hash, func_ptr);
  }

  // Call function
  // AOT functions signature: void func(VMValue* result_ptr)
  VMValue result = VM_VOID();
  func_ptr(&result);
  return result;
}

// call(name, arg) — dynamic dispatch that passes ONE argument to the target.
//
// Resolution is identical to aot_call_dynamic (same `t_<name>` symbol + cache);
// only the call shape differs. The target is invoked as a 1-param AOT function
// `void t_name(VMValue* result, VMValue* arg0)`. Passing the arg to a handler
// that declares NO parameters is ABI-safe on every platform we target — the
// extra register/word is simply ignored by the callee — so Wings can hand every
// handler the request object and 0-arg handlers just don't read it. This is what
// lets you write either `func list_users()` or `func get_user(req)`.
VMValue aot_call_dynamic_1(VMValue func_name, VMValue arg) {
  if (!IS_STRING(func_name)) {
    printf("%s\n", tulpar::i18n::tr_en("Calisma Zamani Hatasi: call() string bekler",
                                       "Runtime Error: call() expects string"));
    return VM_VOID();
  }

  ObjString *str = AS_STRING(func_name);
  char *original_name = str->chars;
  size_t orig_len = (size_t)str->length;

  uint32_t hash = aot_call_hash(original_name, orig_len);
  void (*func_ptr)(VMValue *) = aot_call_cache_lookup(original_name, orig_len, hash);

  if (!func_ptr) {
    char name[256];
    if (strcmp(original_name, "main") == 0) {
      snprintf(name, sizeof(name), "main");
    } else {
      snprintf(name, sizeof(name), "t_%s", original_name);
    }
    func_ptr = (void (*)(VMValue *))tulpar_dlsym(TULPAR_RTLD_DEFAULT, name);
    if (!func_ptr) {
      func_ptr = (void (*)(VMValue *))tulpar_dlsym(TULPAR_RTLD_DEFAULT, original_name);
    }
    if (!func_ptr) {
      printf("%s '%s' (AOT)\n",
             tulpar::i18n::tr_en("Calisma Zamani Hatasi: Fonksiyon bulunamadi",
                                 "Runtime Error: Function not found"),
             name);
      return VM_VOID();
    }
    aot_call_cache_insert(original_name, orig_len, hash, func_ptr);
  }

  VMValue result = VM_VOID();
  ((void (*)(VMValue *, VMValue *))func_ptr)(&result, &arg);
  return result;
}

// AOT runtime initialization (locale/UTF-8, console modes)
void aot_runtime_init(void) {
  static int initialized = 0;
  if (initialized)
    return;
  initialized = 1;

  setlocale(LC_ALL, ".UTF8");

#if PLATFORM_WINDOWS
  // Winsock must be started before any socket() call. Without this,
  // socket() in aot_socket_server / aot_socket_client returns
  // WSANOTINITIALISED (10093) and every wings/router server example
  // fails to bind.
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

static AOTArenaBlock *aot_arena_new_block(size_t min_size) {
  size_t size =
      min_size > AOT_ARENA_BLOCK_SIZE ? min_size : AOT_ARENA_BLOCK_SIZE;
  AOTArenaBlock *block = static_cast<AOTArenaBlock*>(malloc(sizeof(AOTArenaBlock)));
  if (!block)
    return nullptr;

  block->memory = static_cast<char*>(malloc(size));
  if (!block->memory) {
    free(block);
    return nullptr;
  }
  block->size = size;
  block->used = 0;
  block->next = nullptr;
  return block;
}

static void aot_arena_init(void) {
  if (g_aot_string_arena)
    return;

  g_aot_string_arena = static_cast<AOTArena*>(malloc(sizeof(AOTArena)));
  if (!g_aot_string_arena)
    return;

  g_aot_string_arena->head = aot_arena_new_block(AOT_ARENA_BLOCK_SIZE);
  g_aot_string_arena->current = g_aot_string_arena->head;
  g_aot_string_arena->total_allocated = 0;
}

// Fast arena allocation - no free needed until arena reset
static void *aot_arena_alloc(size_t size) {
  if (!g_aot_string_arena)
    aot_arena_init();
  if (!g_aot_string_arena || !g_aot_string_arena->current)
    return malloc(size);

  // Align size
  size = (size + AOT_ARENA_ALIGNMENT - 1) & ~(AOT_ARENA_ALIGNMENT - 1);

  AOTArenaBlock *block = g_aot_string_arena->current;

  // Check if current block has space
  if (block->used + size > block->size) {
    // Re-use the next block in the chain when possible — after a
    // checkpoint restore (`aot_arena_restore`), `block->next` holds an
    // already-allocated 1MB block whose `used` is 0. Plain bump
    // allocation should reach for that *before* mallocing a fresh
    // block. Without this, every restore-then-grow would overwrite
    // `block->next` with a new pointer and leak the old chain.
    if (block->next && size <= block->next->size) {
      g_aot_string_arena->current = block->next;
      block = block->next;
      block->used = 0;  // defensive — should already be 0 post-restore
    } else {
      AOTArenaBlock *new_block = aot_arena_new_block(size);
      if (!new_block)
        return malloc(size); // Fallback

      // Splice into the chain rather than dropping `block->next`.
      new_block->next = block->next;
      block->next = new_block;
      g_aot_string_arena->current = new_block;
      block = new_block;
    }
  }

  void *ptr = block->memory + block->used;
  block->used += size;
  g_aot_string_arena->total_allocated += size;

  return ptr;
}

// Reset arena (reuse memory without freeing)
void aot_arena_reset(void) {
  if (!g_aot_string_arena)
    return;

  // Just reset used counters, keep memory
  AOTArenaBlock *block = g_aot_string_arena->head;
  while (block) {
    block->used = 0;
    block = block->next;
  }
  g_aot_string_arena->current = g_aot_string_arena->head;
  g_aot_string_arena->total_allocated = 0;
}

// ---------------------------------------------------------------------------
// Scoped arena reset (checkpoint stack).
//
// Long-running servers (Wings/Router) build up arena memory each request:
// every parsed VMObject, every response string, every JSON serialisation
// is arena-allocated. Without scoping, a server doing 10k req/sec would
// consume gigabytes after a minute.
//
// `arena_save()` returns an opaque handle to the current arena tip;
// `arena_restore(handle)` rolls allocations forward of that tip back so
// the next request reuses the same blocks. Static state allocated BEFORE
// the save (route registrations, default headers, module-level globals)
// stays intact.
//
// Recycle policy: we don't actually free freed blocks — `used` is just
// reset to 0. The next allocation re-uses them in O(1) without thrashing
// malloc. For genuine memory release, `aot_arena_reset()` (which truncates
// everything) is still the right call.
//
// The checkpoint stack itself is bounded at 32 to keep accidental save-
// without-restore from leaking arbitrary memory; HTTP servers only ever
// need 1 outstanding checkpoint, so 32 is generous.
// ---------------------------------------------------------------------------

typedef struct {
  AOTArenaBlock *block;  // block we were appending to at save time
  size_t used;            // its `used` value at save time
  size_t region_mark;     // g_region size at save time (malloc-object region)
} AOTArenaCheckpoint;

#define AOT_ARENA_CHECKPOINT_MAX 32
// Per-thread checkpoint stack — has to follow the per-thread arena.
// A single shared stack would point at one thread's arena blocks while
// another thread tried to restore against its own.
static thread_local AOTArenaCheckpoint g_arena_checkpoints[AOT_ARENA_CHECKPOINT_MAX];
static thread_local int g_arena_checkpoint_top = 0;

// ---------------------------------------------------------------------------
// Per-request malloc-object region (leak fix — benchmarks/WINGS_VS_FASTAPI.md)
//
// In AOT (vm == nullptr) every object/array LITERAL is malloc'd with
// arena_allocated = 0 and, with no VM object list to chain into, was never
// freed: a steady per-request leak. We now chain those malloc'd containers
// into a per-thread region that arena_save/arena_restore bracket, so a
// request's transient containers are freed in lockstep with its arena
// strings. Only literals allocated *inside* an arena scope are tracked —
// top-level globals (allocated before any save) and the deep copies made by
// aot_persist / string_pin are never tracked, so persistent data is untouched.
//
// `g_region_set` gives the runtime write barrier an O(1) "is this object
// transient?" test: a store of a transient value into a persistent container
// (a global, or an already-persisted object) deep-copies the value so it
// survives the next arena_restore. That barrier — value-flow based — is what
// makes the freeing safe even when a global is aliased through a local.
VMValue aot_persist(VMValue v); // defined below; used by the write barrier

static thread_local std::vector<Obj *> g_region;
static thread_local std::unordered_set<Obj *> g_region_set;

static inline void region_track(Obj *o) {
  if (g_arena_checkpoint_top <= 0 || !o)
    return; // outside any request scope → permanent (e.g. top-level globals)
  g_region.push_back(o);
  g_region_set.insert(o);
}

// A value is "transient" if it lives only until the next arena_restore: arena
// memory, or a malloc container tracked in the current region. Persistent
// objects (top-level globals, aot_persist/string_pin copies) are neither.
static inline bool obj_is_transient(Obj *o) {
  return o && (o->arena_allocated || g_region_set.count(o));
}

// Free a tracked malloc container plus its own malloc'd backing buffers. Does
// NOT recurse: nested containers are tracked separately, and element strings
// are arena-owned (reclaimed by the arena rewind).
static inline void region_free_one(Obj *o) {
  if (!o || o->arena_allocated)
    return;
  if (o->type == OBJ_OBJECT) {
    ObjObject *x = (ObjObject *)o;
    free(x->keys);
    free(x->values);
  } else if (o->type == OBJ_ARRAY) {
    free(((ObjArray *)o)->items);
  }
  free(o);
}

// Runtime write barrier: storing a transient value into a persistent container
// deep-copies it to permanent storage. No-op on the hot path (transient
// container ← anything), so building a response costs nothing.
static inline VMValue wb_persist_escape(Obj *container, VMValue v) {
  if (!container || obj_is_transient(container))
    return v; // container itself is transient → freed together with v
  if (!IS_OBJ(v) || !obj_is_transient(AS_OBJ(v)))
    return v; // scalar, or value already persistent
  return aot_persist(v);
}

VMValue aot_arena_save(void) {
  if (!g_aot_string_arena) aot_arena_init();
  if (!g_aot_string_arena || g_arena_checkpoint_top >= AOT_ARENA_CHECKPOINT_MAX)
    return VM_INT(-1);
  int idx = g_arena_checkpoint_top++;
  g_arena_checkpoints[idx].block = g_aot_string_arena->current;
  g_arena_checkpoints[idx].used =
      g_aot_string_arena->current ? g_aot_string_arena->current->used : 0;
  g_arena_checkpoints[idx].region_mark = g_region.size();
  return VM_INT(idx);
}

// Roll the arena (and the malloc-object region) back to checkpoint `idx`.
// Shared by arena_restore (keeps the checkpoint) and arena_drop (releases it).
static void aot_arena_rewind_to(int idx) {
  AOTArenaCheckpoint *cp = &g_arena_checkpoints[idx];
  if (cp->block) {
    // Zero out every block AFTER the checkpoint's block: keep the
    // memory mapping but reset `used` so the next alloc reuses it.
    for (AOTArenaBlock *b = cp->block->next; b; b = b->next) {
      b->used = 0;
    }
    // Inside the checkpoint's block, rewind to where we were when
    // saved. Anything allocated since then is now logically free.
    cp->block->used = cp->used;
    g_aot_string_arena->current = cp->block;
  }
  // Free the malloc'd containers tracked since this checkpoint (a request's
  // transient objects/arrays), in lockstep with the arena string rewind.
  // Persisted/global objects were never tracked, so they survive untouched.
  for (size_t i = cp->region_mark; i < g_region.size(); i++) {
    g_region_set.erase(g_region[i]);
    region_free_one(g_region[i]);
  }
  if (cp->region_mark < g_region.size())
    g_region.resize(cp->region_mark);
}

VMValue aot_arena_restore(VMValue idxVal) {
  if (!IS_INT(idxVal) || !g_aot_string_arena) return VM_INT(0);
  int idx = (int)AS_INT(idxVal);
  if (idx < 0 || idx >= g_arena_checkpoint_top) return VM_INT(0);
  aot_arena_rewind_to(idx);
  // Drop any *nested* checkpoints (idx+1..top) — those scopes have
  // just been wiped — but KEEP the one we restored to so the caller
  // can roll back to it again. Wings/Router relies on this: a single
  // top-of-listen() `arena_save` is restored after every request.
  g_arena_checkpoint_top = idx + 1;
  return VM_INT(0);
}

// arena_drop(handle): like arena_restore, but RELEASES the checkpoint (and any
// nested above it) instead of keeping it. The scope-exit counterpart of
// arena_save — a function that does `wm = arena_save(); ...; arena_drop(wm)` is
// balanced, so the 32-slot checkpoint stack does NOT grow when that function is
// called repeatedly in the same thread. Wings pool workers (one serve call per
// connection) and the evented loop (one per request) need this: with plain
// arena_restore the stack exhausted after 32 calls, after which every
// arena_save returned -1 and every arena_restore no-op'd → the per-request
// arena/region stopped being reclaimed → unbounded leak under connection churn.
VMValue aot_arena_drop(VMValue idxVal) {
  if (!IS_INT(idxVal) || !g_aot_string_arena) return VM_INT(0);
  int idx = (int)AS_INT(idxVal);
  if (idx < 0 || idx >= g_arena_checkpoint_top) return VM_INT(0);
  aot_arena_rewind_to(idx);
  g_arena_checkpoint_top = idx; // release this checkpoint, not just rewind it
  return VM_INT(0);
}

// Free all arena memory
void aot_arena_destroy(void) {
  if (!g_aot_string_arena)
    return;

  AOTArenaBlock *block = g_aot_string_arena->head;
  while (block) {
    AOTArenaBlock *next = block->next;
    free(block->memory);
    free(block);
    block = next;
  }
  free(g_aot_string_arena);
  g_aot_string_arena = nullptr;
}

// ============================================================
// FAST STRING ALLOCATION using AOT Arena
// ============================================================

// Helper for AOT string allocation (no GC) - NOW USES ARENA
static ObjString *aot_allocate_string(const char *chars, int length) {
  // Single arena allocation for struct + chars together (cache friendly)
  size_t total_size = sizeof(ObjString) + length + 1;
  char *block = static_cast<char *>(aot_arena_alloc(total_size));
  if (!block)
    return nullptr;

  ObjString *str = (ObjString *)block;
  str->obj.type = OBJ_STRING;
  str->obj.arena_allocated = 1; // Mark as arena allocated
  str->obj.next = nullptr;
  str->obj.ref_count = 1;
  str->obj.is_moved = 0;
  str->length = length;
  str->capacity = length + 1;

  // Chars immediately follow struct
  str->chars = block + sizeof(ObjString);
  memcpy(str->chars, chars, length);
  str->chars[length] = '\0';

  // Lazy hash - compute only when needed
  str->hash = 0;

  return str;
}

// string_pin(s) -> str
//
// Copies an arena-allocated string into permanent (malloc'd) storage
// and returns a new ObjString whose lifetime is the process. Used by
// the wings response cache: the cached wire bytes must outlive the
// per-request arena (which is reset via `arena_restore` after every
// `socket_send`) — without pinning, the next request's allocations
// stomp the cache's char buffer and subsequent reads return garbage.
//
// The malloc-based ObjString is marked `arena_allocated = 0`, so any
// future GC pass (if/when AOT gains one) skips it; today it leaks for
// the process lifetime, which is bounded by `unique_cached_routes ×
// response_size` and intentional.
VMValue aot_string_pin(VMValue strVal) {
  if (!IS_STRING(strVal)) return strVal;
  ObjString *src = AS_STRING(strVal);

  // Single contiguous malloc: ObjString header + char payload + NUL.
  ObjString *pinned = (ObjString *)malloc(sizeof(ObjString) + src->length + 1);
  if (!pinned) return strVal; // OOM — fall back to arena string

  pinned->obj.type = OBJ_STRING;
  pinned->obj.arena_allocated = 0; // permanent — not part of arena
  pinned->obj.next = nullptr;
  pinned->obj.ref_count = 1;
  pinned->obj.is_moved = 0;
  pinned->length = src->length;
  pinned->capacity = src->length + 1;
  pinned->chars = (char *)(pinned + 1);
  memcpy(pinned->chars, src->chars, src->length);
  pinned->chars[src->length] = '\0';
  pinned->hash = 0;

  return VM_OBJ((Obj *)pinned);
}

// Wrapper for AOT string literals (matches vm_alloc_string signature)
ObjString *vm_alloc_string_aot(void *vm, const char *chars, int length) {
  return aot_allocate_string(chars, length);
}

// persist(value) -> value
//
// Deep-copies a value into permanent (malloc'd, ARC-managed) storage that
// survives an `arena_restore`. This is the escape hatch for handler code that
// keeps request-built data in a long-lived global — the in-memory "database"
// pattern: `push(_users, persist(u))`. Without it, the per-request arena reset
// reclaims `u`'s memory and the global ends up holding a dangling pointer
// (→ crash on the next read).
//
// Scalars (int/float/bool/void) are returned by value — no heap, nothing to
// copy. Strings, arrays and objects are rebuilt recursively with malloc and
// `arena_allocated = 0` so the arena reset skips them. They live as long as
// something references them (today effectively process lifetime; a future GC
// would reclaim via ref_count).
static ObjString *aot_persist_string_obj(ObjString *src) {
  ObjString *p = (ObjString *)malloc(sizeof(ObjString) + src->length + 1);
  if (!p) return src;
  p->obj.type = OBJ_STRING;
  p->obj.arena_allocated = 0;
  p->obj.next = nullptr;
  p->obj.ref_count = 1;
  p->obj.is_moved = 0;
  p->length = src->length;
  p->capacity = src->length + 1;
  p->chars = (char *)(p + 1);
  memcpy(p->chars, src->chars, src->length);
  p->chars[src->length] = '\0';
  p->hash = 0;
  return p;
}

VMValue aot_persist(VMValue v) {
  if (IS_STRING(v)) {
    return VM_OBJ((Obj *)aot_persist_string_obj(AS_STRING(v)));
  }
  if (IS_ARRAY(v)) {
    ObjArray *src = AS_ARRAY(v);
    ObjArray *dst = (ObjArray *)malloc(sizeof(ObjArray));
    if (!dst) return v;
    dst->obj.type = OBJ_ARRAY;
    dst->obj.arena_allocated = 0;
    dst->obj.next = nullptr;
    dst->obj.ref_count = 1;
    dst->obj.is_moved = 0;
    int n = src->count;
    dst->count = n;
    dst->capacity = n;
    dst->items = (n > 0) ? (VMValue *)malloc(sizeof(VMValue) * n) : nullptr;
    for (int i = 0; i < n; i++) {
      dst->items[i] = aot_persist(src->items[i]);
    }
    return VM_OBJ((Obj *)dst);
  }
  if (IS_OBJECT(v)) {
    ObjObject *src = AS_OBJECT(v);
    ObjObject *dst = (ObjObject *)malloc(sizeof(ObjObject));
    if (!dst) return v;
    dst->obj.type = OBJ_OBJECT;
    dst->obj.arena_allocated = 0;
    dst->obj.next = nullptr;
    dst->obj.ref_count = 1;
    dst->obj.is_moved = 0;
    int n = src->count;
    dst->count = n;
    dst->capacity = n;
    if (n > 0) {
      dst->keys = (ObjString **)malloc(sizeof(ObjString *) * n);
      dst->values = (VMValue *)malloc(sizeof(VMValue) * n);
      for (int i = 0; i < n; i++) {
        dst->keys[i] =
            src->keys[i] ? aot_persist_string_obj(src->keys[i]) : nullptr;
        dst->values[i] = aot_persist(src->values[i]);
      }
    } else {
      dst->keys = nullptr;
      dst->values = nullptr;
    }
    return VM_OBJ((Obj *)dst);
  }
  // Scalars and any other value type: copy by value, no heap.
  return v;
}

VMValue aot_persist_ptr(VMValue *v) {
  if (!v) return VM_VOID();
  return aot_persist(*v);
}

// Wrapper for AOT print (takes pointer to VMValue, calls vm_print_value which
// takes value)
#include <cstddef>
void aot_print_value(VMValue *v) { vm_print_value(*v); }

// ============================================================
// FAST STRING CONCATENATION (for AOT optimization)
// Uses Arena allocator for O(1) allocation overhead
// ============================================================

// Fast string append - ultra-optimized version with Arena
// Defined further down (next to the toString builtin); forward-declared so the
// concat path can coerce a non-string operand to its string form.
VMValue aot_to_string(VMValue value);

VMValue aot_string_concat_fast(VMValue a, VMValue b) {
  // `+` with a string on either side is concatenation: coerce the other
  // operand via toString so `"n = " + 5` yields "n = 5" instead of a
  // silently-wrong 0. (string+string skips coercion — the common fast path.)
  VMValue sa = IS_STRING(a) ? a : aot_to_string(a);
  VMValue sb = IS_STRING(b) ? b : aot_to_string(b);
  if (!IS_STRING(sa) || !IS_STRING(sb)) {
    return VM_INT(0); // defensive: aot_to_string always returns a string
  }

  ObjString *s1 = AS_STRING(sa);
  ObjString *s2 = AS_STRING(sb);

  int len1 = s1->length;
  int len2 = s2->length;
  int total_len = len1 + len2;

  // Single AOT ARENA allocation for both struct and chars
  size_t alloc_size = sizeof(ObjString) + total_len + 1;
  char *block = static_cast<char *>(aot_arena_alloc(alloc_size));
  if (!block)
    return VM_INT(0);

  ObjString *result = (ObjString *)block;
  result->obj.type = OBJ_STRING;
  result->obj.arena_allocated = 1;
  result->obj.next = nullptr;
  result->obj.ref_count = 1;
  result->obj.is_moved = 0;
  result->length = total_len;
  result->capacity = total_len + 1;
  result->chars = block + sizeof(ObjString);

  // Direct memory copy
  memcpy(result->chars, s1->chars, len1);
  memcpy(result->chars + len1, s2->chars, len2);
  result->chars[total_len] = '\0';

  // Lazy hash
  result->hash = 0;

  return VM_OBJ((Obj *)result);
}

VMValue aot_string_concat_fast_ptr(VMValue *a_ptr, VMValue *b_ptr) {
  if (!a_ptr || !b_ptr)
    return VM_INT(0);
  return aot_string_concat_fast(*a_ptr, *b_ptr);
}

// Create string from C string literal (fast path)
VMValue aot_string_from_cstr(const char *cstr) {
  int len = strlen(cstr);
  ObjString *str = aot_allocate_string(cstr, len);
  return VM_OBJ((Obj *)str);
}

// Pre-allocated string builder for loops
typedef struct {
  char *buffer;
  int length;
  int capacity;
} StringBuilder;

StringBuilder *aot_stringbuilder_new(int initial_capacity) {
  StringBuilder *sb = static_cast<StringBuilder*>(malloc(sizeof(StringBuilder)));
  sb->capacity = initial_capacity > 64 ? initial_capacity : 64;
  sb->buffer = static_cast<char*>(malloc(sb->capacity));
  sb->buffer[0] = '\0';
  sb->length = 0;
  return sb;
}

void aot_stringbuilder_append(StringBuilder *sb, const char *str, int len) {
  if (sb->length + len >= sb->capacity) {
    // Double capacity
    sb->capacity = (sb->length + len + 1) * 2;
    sb->buffer = static_cast<char*>(realloc(sb->buffer, sb->capacity));
  }
  memcpy(sb->buffer + sb->length, str, len);
  sb->length += len;
  sb->buffer[sb->length] = '\0';
}

// Append VMValue to StringBuilder (handles any type)
void aot_stringbuilder_append_vmvalue(StringBuilder *sb, VMValue val) {
  if (IS_STRING(val)) {
    ObjString *str = AS_STRING(val);
    aot_stringbuilder_append(sb, str->chars, str->length);
  } else if (IS_INT(val)) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(val));
    aot_stringbuilder_append(sb, buf, len);
  } else if (IS_FLOAT(val)) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%g", AS_FLOAT(val));
    aot_stringbuilder_append(sb, buf, len);
  } else if (IS_BOOL(val)) {
    const char *s = AS_BOOL(val) ? "true" : "false";
    aot_stringbuilder_append(sb, s, strlen(s));
  }
}

VMValue aot_stringbuilder_to_string(StringBuilder *sb) {
  ObjString *str = aot_allocate_string(sb->buffer, sb->length);
  return VM_OBJ((Obj *)str);
}

void aot_stringbuilder_free(StringBuilder *sb) {
  free(sb->buffer);
  free(sb);
}

// Helper for binary operations with mixed types
// Corresponds to BINARY_OP macro in vm.c
void vm_binary_op(VM *vm, VMValue *a_ptr, VMValue *b_ptr, int op_token,
                  VMValue *result) {
  VMValue a = *a_ptr;
  VMValue b = *b_ptr;

  // Use the same Type Pair logic as VM
  uint8_t type_pair = TYPE_PAIR(a, b);

  switch (op_token) {
  case TOKEN_PLUS: // +
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_INT(AS_INT(a) + AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_FLOAT(AS_FLOAT(a) + AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_FLOAT((double)AS_INT(a) + AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_FLOAT(AS_FLOAT(a) + (double)AS_INT(b));
      return;
    default: {
      // String concatenation when either side is a string — the concat
      // helper coerces the non-string operand (so `"n = " + 5` → "n = 5"
      // rather than a silently-wrong 0).
      if (IS_STRING(a) || IS_STRING(b)) {
        *result = aot_string_concat_fast(a, b);
        return;
      }
      *result = VM_INT(0);
      return;
    }
    }

  case TOKEN_MINUS: // -
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_INT(AS_INT(a) - AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_FLOAT(AS_FLOAT(a) - AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_FLOAT((double)AS_INT(a) - AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_FLOAT(AS_FLOAT(a) - (double)AS_INT(b));
      return;
    default:
      *result = VM_INT(0);
      return;
    }

  case TOKEN_MULTIPLY: // *
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_INT(AS_INT(a) * AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_FLOAT(AS_FLOAT(a) * AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_FLOAT((double)AS_INT(a) * AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_FLOAT(AS_FLOAT(a) * (double)AS_INT(b));
      return;
    default:
      *result = VM_INT(0);
      return;
    }

  case TOKEN_DIVIDE: // /
    switch (type_pair) {
    case TYPE_INT_INT:
      if (AS_INT(b) == 0) {
        printf("%s\n", tulpar::i18n::tr_en("Calisma Zamani Hatasi: Sifira bolme",
                                           "Runtime Error: Division by zero"));
        *result = VM_INT(0);
        return;
      }
      *result = VM_INT(AS_INT(a) / AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_FLOAT(AS_FLOAT(a) / AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_FLOAT((double)AS_INT(a) / AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_FLOAT(AS_FLOAT(a) / (double)AS_INT(b));
      return;
    default:
      *result = VM_INT(0);
      return;
    }

  case TOKEN_LESS: // <
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) < AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) < AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) < AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) < (double)AS_INT(b));
      return;
    default:
      *result = VM_BOOL(0);
      return;
    }

  case TOKEN_GREATER: // >
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) > AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) > AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) > AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) > (double)AS_INT(b));
      return;
    default:
      *result = VM_BOOL(0);
      return;
    }

  case TOKEN_LESS_EQUAL: // <=
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) <= AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) <= AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) <= AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) <= (double)AS_INT(b));
      return;
    default:
      *result = VM_BOOL(0);
      return;
    }

  case TOKEN_GREATER_EQUAL: // >=
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) >= AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) >= AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) >= AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) >= (double)AS_INT(b));
      return;
    default:
      *result = VM_BOOL(0);
      return;
    }

  case TOKEN_EQUAL: // ==
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) == AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) == AS_FLOAT(b));
      return;
    case TYPE_BOOL_BOOL:
      *result = VM_BOOL(AS_BOOL(a) == AS_BOOL(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) == AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) == (double)AS_INT(b));
      return;
    default: {
      if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *s1 = AS_STRING(a);
        ObjString *s2 = AS_STRING(b);
        *result = VM_BOOL(strcmp(s1->chars, s2->chars) == 0);
        return;
      }
      *result = VM_BOOL(0);
      return;
    }
    }

  case TOKEN_NOT_EQUAL: // !=
  {
    bool eq = false;
    switch (type_pair) {
    case TYPE_INT_INT:
      eq = AS_INT(a) == AS_INT(b);
      break;
    case TYPE_FLOAT_FLOAT:
      eq = AS_FLOAT(a) == AS_FLOAT(b);
      break;
    case TYPE_BOOL_BOOL:
      eq = AS_BOOL(a) == AS_BOOL(b);
      break;
    case TYPE_INT_FLOAT:
      eq = (double)AS_INT(a) == AS_FLOAT(b);
      break;
    case TYPE_FLOAT_INT:
      eq = AS_FLOAT(a) == (double)AS_INT(b);
      break;
    default: {
      if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *s1 = AS_STRING(a);
        ObjString *s2 = AS_STRING(b);
        eq = strcmp(s1->chars, s2->chars) == 0;
      } else {
        eq = false;
      }
      break;
    }
    }
    *result = VM_BOOL(!eq);
    return;
  }

  default:
    *result = VM_INT(0);
    return;
  }
}

// Array Wrappers
void vm_array_push_wrapper(VM *vm, ObjArray *array, VMValue value) {
  if (!array)
    return;
  vm_array_push(vm, array, value);
}

VMValue vm_array_get(ObjArray *array, int index) {
  if (!array || index < 0 || index >= array->count) {
    printf("%s\n",
           tulpar::i18n::tr_en("Calisma Zamani Hatasi: Dizi indeksi sinir disinda",
                               "Runtime Error: Array index out of bounds"));
    return VM_INT(0);
  }
  return array->items[index];
}

void vm_array_set(ObjArray *array, int index, VMValue value) {
  if (array)
    value = wb_persist_escape((Obj *)array, value);
  if (!array || index < 0 || index >= array->count) {
    printf("%s\n",
           tulpar::i18n::tr_en("Calisma Zamani Hatasi: Dizi indeksi sinir disinda",
                               "Runtime Error: Array index out of bounds"));
    return;
  }
  array->items[index] = value;
}

// Pointer-based wrapper used by AOT closure-env codegen. The 16-byte
// VMValue must cross the C boundary by pointer (mirroring
// vm_array_push_aot_ptr_wrapper): passing it by value drops the payload
// eightbyte on the SysV/MinGW struct-arg ABI, which silently corrupted
// every captured variable to 0.
void vm_array_set_aot_ptr_wrapper(ObjArray *array, int index,
                                  VMValue *value_ptr) {
  if (!value_ptr)
    return;
  vm_array_set(array, index, *value_ptr);
}

// ============================================================
// VALUE-BASED ARRAY ACCESS (no alloca needed, stack-safe)
// These functions take/return VMValue directly for efficiency
// ============================================================

// Enable/disable bounds checking for performance testing
#ifndef TULPAR_UNSAFE_ARRAYS
#define TULPAR_UNSAFE_ARRAYS 0 // Set to 1 to disable bounds check
#endif

// Fast array get - takes VMValue array and int index, returns VMValue
VMValue aot_array_get_fast(VMValue arr_val, int64_t index) {
#if TULPAR_UNSAFE_ARRAYS
  // UNSAFE MODE: No type check, no bounds check - MAXIMUM SPEED
  ObjArray *arr = AS_ARRAY(arr_val);
  return arr->items[index];
#else
  // SAFE MODE: Full checks
  if (!IS_ARRAY(arr_val)) {
    return VM_INT(0);
  }
  ObjArray *arr = AS_ARRAY(arr_val);
  if (index < 0 || index >= arr->count) {
    return VM_INT(0);
  }
  return arr->items[index];
#endif
}

// Fast array set - takes VMValue array, int index, VMValue value
void aot_array_set_fast(VMValue arr_val, int64_t index, VMValue value) {
  if (IS_OBJ(arr_val))
    value = wb_persist_escape(AS_OBJ(arr_val), value);
#if TULPAR_UNSAFE_ARRAYS
  // UNSAFE MODE: No type check, no bounds check
  ObjArray *arr = AS_ARRAY(arr_val);
  arr->items[index] = value;
#else
  // SAFE MODE: Full checks
  if (!IS_ARRAY(arr_val)) {
    return;
  }
  ObjArray *arr = AS_ARRAY(arr_val);
  if (index < 0 || index >= arr->count) {
    return;
  }
  arr->items[index] = value;
#endif
}

// ============================================================
// UNSAFE ARRAY ACCESS - NO BOUNDS CHECK (for benchmarking)
// WARNING: Use only when you're 100% sure indices are valid!
// ============================================================

// Unsafe array get - NO type check, NO bounds check
// Assumes arr_val is definitely an array and index is in range
TULPAR_INLINE static VMValue
aot_array_get_unsafe(VMValue arr_val, int64_t index) {
  ObjArray *arr = AS_ARRAY(arr_val); // Direct cast, no check
  return arr->items[index];
}

// Unsafe array set - NO type check, NO bounds check
TULPAR_INLINE static void
aot_array_set_unsafe(VMValue arr_val, int64_t index, VMValue value) {
  ObjArray *arr = AS_ARRAY(arr_val); // Direct cast, no check
  arr->items[index] = value;
}

// ============================================================
// RAW POINTER ARRAY ACCESS - Maximum performance
// Works directly with ObjArray pointer, skips VMValue wrapper
// ============================================================

// Get raw array pointer from VMValue (one-time conversion)
ObjArray *aot_array_get_raw(VMValue arr_val) {
  if (!IS_ARRAY(arr_val))
    return nullptr;
  return AS_ARRAY(arr_val);
}

// Direct array element access via raw pointer - FASTEST!
TULPAR_INLINE static VMValue
aot_raw_get(ObjArray *arr, int64_t index) {
  return arr->items[index];
}

TULPAR_INLINE static void
aot_raw_set(ObjArray *arr, int64_t index, VMValue value) {
  arr->items[index] = value;
}

// Public versions for LLVM to call (non-inline)
VMValue aot_array_get_raw_fast(ObjArray *arr, int64_t index) {
  return arr->items[index];
}

void aot_array_set_raw_fast(ObjArray *arr, int64_t index, VMValue value) {
  arr->items[index] = wb_persist_escape((Obj *)arr, value);
}

// Object Wrappers
void vm_object_set(VM *vm, ObjObject *obj, char *key, VMValue value) {
  if (!obj || !key)
    return;

  // Write barrier: a transient value stored into a persistent object must be
  // deep-copied so it survives the per-request arena_restore.
  value = wb_persist_escape((Obj *)obj, value);

  // Create string object for key
  int len = strlen(key);
  ObjString *keyObj =
      vm ? vm_alloc_string(vm, key, len) : aot_allocate_string(key, len);

  // Write barrier for the KEY (mirrors the value barrier above): storing into a
  // persistent container with a transient key string would leave a dangling key
  // pointer once the per-request arena is rewound by arena_restore. The value
  // survives via wb_persist_escape, but without this the key name does not —
  // the global reads back with a corrupted/garbage key on the next request
  // (e.g. a global session/token dict `_t[token] = uid` loses its keys). Only
  // copy when the container is persistent and the key is actually transient, so
  // the hot path (transient response objects) stays free.
  if (!obj_is_transient((Obj *)obj) && obj_is_transient((Obj *)keyObj)) {
    keyObj = aot_persist_string_obj(keyObj);
  }

  // Check if key exists
  for (int i = 0; i < obj->count; i++) {
    if (strcmp(obj->keys[i]->chars, key) == 0) {
      obj->values[i] = value;
      return;
    }
  }

  // Resize if needed
  if (obj->count >= obj->capacity) {
    int old_capacity = obj->capacity;
    obj->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    if (obj->obj.arena_allocated) {
      ObjString **new_keys = (ObjString **)aot_arena_alloc(sizeof(ObjString *) * obj->capacity);
      VMValue *new_values = (VMValue *)aot_arena_alloc(sizeof(VMValue) * obj->capacity);
      if (old_capacity > 0) {
        memcpy(new_keys, obj->keys, sizeof(ObjString *) * obj->count);
        memcpy(new_values, obj->values, sizeof(VMValue) * obj->count);
      }
      obj->keys = new_keys;
      obj->values = new_values;
    } else {
      obj->keys =
          (ObjString **)realloc(obj->keys, sizeof(ObjString *) * obj->capacity);
      obj->values =
          static_cast<VMValue*>(realloc(obj->values, sizeof(VMValue) * obj->capacity));
    }
  }

  obj->keys[obj->count] = keyObj;
  obj->values[obj->count] = value;
  obj->count++;
}

VMValue vm_object_get(ObjObject *obj, char *key) {
  if (!obj || !key)
    return VM_INT(0);

  for (int i = 0; i < obj->count; i++) {
    if (strcmp(obj->keys[i]->chars, key) == 0) {
      return obj->values[i];
    }
  }
  return VM_INT(0); // Undefined property
}

// Forward declarations
VMValue vm_get_element(VMValue target, VMValue index);
void vm_set_element(VM *vm, VMValue target, VMValue index, VMValue value);

// Pointer-based version for ABI compatibility with LLVM
VMValue vm_get_element_ptr(VMValue *target, VMValue *index) {
  if (!target || !index)
    return VM_INT(0);
  return vm_get_element(*target, *index);
}

// Pointer-based version for ABI compatibility with LLVM
void vm_set_element_ptr(VM *vm, VMValue *target, VMValue *index,
                        VMValue *value) {
  if (!target || !index || !value) {
    printf("%s\n",
           tulpar::i18n::tr_en("Calisma Zamani Hatasi: set element icin gecersiz pointer",
                               "Runtime Error: nullptr pointer in set element"));
    return;
  }
  vm_set_element(vm, *target, *index, *value);
}

VMValue vm_get_element(VMValue target, VMValue index) {
  if (IS_ARRAY(target)) {
    if (IS_INT(index)) {
      return vm_array_get(AS_ARRAY(target), (int)AS_INT(index));
    }
    // Array indexed by a non-int (string) key has no such entry — return 0
    // silently, matching how a missing object key behaves. (A Wings handler
    // that returns a raw list makes the dispatcher probe result["_stream"] /
    // ["_status"] on an array; that should be a quiet "absent", not an error.)
    return VM_INT(0);
  } else if (IS_OBJECT(target)) {
    if (IS_STRING(index)) {
      return vm_object_get(AS_OBJECT(target), AS_STRING(index)->chars);
    }
  } else if (IS_STRING(target)) {
    if (IS_INT(index)) {
      ObjString *str = AS_STRING(target);
      int idx = (int)AS_INT(index);
      if (idx < 0 || idx >= str->length)
        return VM_OBJ(aot_allocate_string("", 0));
      return VM_OBJ(aot_allocate_string(&str->chars[idx], 1));
    }
  }
  printf("%s\n",
         tulpar::i18n::tr_en(
             "Calisma Zamani Hatasi: get islemi icin gecersiz hedef veya indeks",
             "Runtime Error: Invalid index or target for get access"));
  return VM_INT(0);
}

void vm_set_element(VM *vm, VMValue target, VMValue index, VMValue value) {
  if (IS_ARRAY(target)) {
    if (IS_INT(index)) {
      vm_array_set(AS_ARRAY(target), (int)AS_INT(index), value);
      return;
    }
  } else if (IS_OBJECT(target)) {
    if (IS_STRING(index)) {
      vm_object_set(vm, AS_OBJECT(target), AS_STRING(index)->chars, value);
      return;
    }
  }
  printf("%s\n",
         tulpar::i18n::tr_en(
             "Calisma Zamani Hatasi: set islemi icin gecersiz hedef veya indeks",
             "Runtime Error: Invalid index or target for set access"));
}

// Print a VMValue (used by OP_PRINT in VM)
void print_vm_value(VMValue value) {
  switch (value.type) {
  case VM_VAL_VOID:
    // The `null` literal and a value-less (void) result share this tag;
    // "null" reads better than "void" now that `null` is user-facing.
    printf("null");
    break;
  case VM_VAL_BOOL:
    printf("%s", AS_BOOL(value) ? "true" : "false");
    break;
  case VM_VAL_INT:
    printf("%lld", AS_INT(value));
    break;
  case VM_VAL_FLOAT:
    printf("%g", AS_FLOAT(value));
    break;
  case VM_VAL_OBJ:
    if (IS_STRING(value)) {
      printf("%s", AS_STRING(value)->chars);
    } else if (IS_ARRAY(value)) {
      ObjArray *arr = AS_ARRAY(value);
      printf("[");
      for (int i = 0; i < arr->count; i++) {
        if (i > 0)
          printf(", ");
        print_vm_value(arr->items[i]);
      }
      printf("]");
    } else {
      printf("<object>");
    }
    break;
  default:
    printf("<unknown>");
    break;
  }
}

// Alias for LLVM AOT - prints with newline (takes pointer for ABI
// compatibility)
void print_value(VMValue *value_ptr) {
  if (value_ptr) {
    print_vm_value(*value_ptr);
  }
  printf("\n");
}

// Print without newline - used for inline printing (takes pointer for ABI
// compatibility)
void print_value_inline(VMValue *value_ptr) {
  if (value_ptr) {
    print_vm_value(*value_ptr);
  }
}

// Print newline only
void print_newline(void) {
  printf("\n");
  fflush(stdout);
}

// ============================================================================
// AOT Builtin Functions
// ============================================================================

// toString(VMValue) -> VMValue (String Object)
// MUST be thread_local: under a multi-threaded listener (listen_pool /
// listen_async) two workers calling toString() concurrently would otherwise
// clobber each other's formatted bytes in this scratch buffer. The classic
// symptom was a wings handler building `"... id = " + toString(id)` getting an
// EMPTY toString() result (~1% under load) → malformed SQL → 0 rows → a
// spurious 404. Per-thread storage makes toString() race-free.
static thread_local char aot_string_buffer[1024];

VMValue aot_to_string(VMValue value) {
  switch (value.type) {
  case VM_VAL_INT:
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "%lld",
             AS_INT(value));
    break;
  case VM_VAL_FLOAT:
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "%g",
             AS_FLOAT(value));
    break;
  case VM_VAL_BOOL:
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "%s",
             AS_BOOL(value) ? "true" : "false");
    break;
  case VM_VAL_OBJ:
    if (IS_STRING(value)) {
      return value;
    }
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "<object>");
    break;
  default:
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "nullptr");
    break;
  }

  ObjString *str =
      aot_allocate_string(aot_string_buffer, strlen(aot_string_buffer));
  return VM_OBJ((Obj *)str);
}

// Pointer ABI wrappers for AOT (Windows-friendly)
VMValue aot_to_string_ptr(VMValue *value_ptr) {
  if (!value_ptr)
    return VM_INT(0);
  return aot_to_string(*value_ptr);
}

// toInt(VMValue) -> int64
int64_t aot_to_int(VMValue value) {
  switch (value.type) {
  case VM_VAL_INT:
    return AS_INT(value);
  case VM_VAL_FLOAT:
    return (int64_t)AS_FLOAT(value);
  case VM_VAL_BOOL:
    return AS_BOOL(value) ? 1 : 0;
  case VM_VAL_OBJ:
    if (IS_STRING(value)) {
      return atoll(AS_STRING(value)->chars);
    }
    return 0;
  default:
    return 0;
  }
}

int64_t aot_to_int_ptr(VMValue *value_ptr) {
  if (!value_ptr)
    return 0;
  return aot_to_int(*value_ptr);
}

// toFloat(VMValue) -> double
double aot_to_float(VMValue value) {
  switch (value.type) {
  case VM_VAL_INT:
    return (double)AS_INT(value);
  case VM_VAL_FLOAT:
    return AS_FLOAT(value);
  case VM_VAL_BOOL:
    return AS_BOOL(value) ? 1.0 : 0.0;
  case VM_VAL_OBJ:
    if (IS_STRING(value)) {
      return atof(AS_STRING(value)->chars);
    }
    return 0.0;
  default:
    return 0.0;
  }
}

double aot_to_float_ptr(VMValue *value_ptr) {
  if (!value_ptr)
    return 0.0;
  return aot_to_float(*value_ptr);
}

// len(VMValue) -> int
//
// Strings: byte length (matches String.length).
// Arrays:  element count.
// Objects: key count — `length({a:1, b:2})` returns 2. Used to be 0;
//          changed because every dogfood backend reached for
//          `length(query)` to detect an empty query string and got
//          surprised. Mirrors Python's `len({})`, JS's
//          `Object.keys(o).length`, etc.
// Anything else (int, bool, float, nil): 0.
int64_t aot_len(VMValue value) {
  if (IS_STRING(value)) {
    return AS_STRING(value)->length;
  } else if (IS_ARRAY(value)) {
    return AS_ARRAY(value)->count;
  } else if (IS_OBJECT(value)) {
    return ((ObjObject *)AS_OBJECT(value))->count;
  }
  return 0;
}

int64_t aot_len_ptr(VMValue *value_ptr) {
  if (!value_ptr)
    return 0;
  return aot_len(*value_ptr);
}

// push(array, value) - wrapper for AOT (pointer ABI for Windows)
void aot_array_push(VMValue *arr_ptr, VMValue *item_ptr) {
  VMValue arr_val = *arr_ptr;
  VMValue item = *item_ptr;
  if (IS_ARRAY(arr_val)) {
    ObjArray *arr = AS_ARRAY(arr_val);
    // Write barrier: a transient item pushed into a persistent array is
    // deep-copied so it outlives the per-request arena_restore.
    item = wb_persist_escape((Obj *)arr, item);
    // Inline push without VM
    if (arr->count >= arr->capacity) {
      int old_capacity = arr->capacity;
      int new_cap = old_capacity < 8 ? 8 : old_capacity * 2;
      if (arr->obj.arena_allocated) {
        VMValue *new_items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
        if (old_capacity > 0) {
          memcpy(new_items, arr->items, sizeof(VMValue) * arr->count);
        }
        arr->items = new_items;
      } else {
        arr->items =
            static_cast<VMValue *>(realloc(arr->items, sizeof(VMValue) * new_cap));
      }
      arr->capacity = new_cap;
    }
    arr->items[arr->count++] = item;
  }
}

// pop(array) -> VMValue
VMValue aot_array_pop(VMValue arr_val) {
  if (IS_ARRAY(arr_val) && AS_ARRAY(arr_val)->count > 0) {
    ObjArray *arr = AS_ARRAY(arr_val);
    return arr->items[--arr->count];
  }
  return VM_INT(0);
}

// ============================================================================
// ObjStruct heap-promoted typed structs (Plan 04 v2)
//
// Stack-typed structs (PR3..PR6) live as scalarized LLVM allocas. That's
// the fastest path but the values can't escape — `push(arr, p)` and
// `obj["k"] = p` need a heap pointer. The escape sites in llvm_backend.cpp
// call `aot_struct_alloc` / `aot_struct_alloc_from_fields` to lift the
// stack contents into a heap `ObjStruct` and pass the boxed VMValue
// downstream. Field reads on a struct VMValue go through
// `aot_struct_get_field`.
// ============================================================================

// Allocate an empty heap struct. `field_count` slots, all zero-initialised.
// Type name is interned by the AOT module, lifetime ≥ struct.
extern "C" VMValue aot_struct_alloc(const char *type_name, int field_count) {
  if (field_count < 0) field_count = 0;
  // ObjStruct already reserves one i64 slot via the flexible-array
  // member, so allocate `(N-1)` extra slots (0 when N <= 1).
  size_t extra =
      (field_count > 1) ? (size_t)(field_count - 1) * sizeof(int64_t) : 0;
  // Arena-allocate so a 1M-iter `push(arr, struct)` loop becomes 1M
  // bump-pointer increments instead of 1M mallocs. Two correctness
  // notes:
  //   * The previous malloc path had no free site (no GC sweep, no
  //     ref_count drop wired up for ObjStruct), so every push leaked a
  //     ~40-byte block until process exit. Arena alloc preserves that
  //     "live until program ends" behaviour for non-arena-scoped code
  //     and ADDS proper cleanup at request boundaries in wings (which
  //     calls aot_arena_restore between requests).
  //   * `arena_allocated = 1` flags downstream paths (any future free)
  //     to skip libc free() — arena memory must not be passed to free.
  ObjStruct *s = static_cast<ObjStruct *>(aot_arena_alloc(sizeof(ObjStruct) + extra));
  if (!s) return VM_INT(0);
  s->obj.type = OBJ_STRUCT;
  s->obj.next = nullptr;
  s->obj.arena_allocated = 1;
  s->obj.ref_count = 1;
  s->obj.is_moved = 0;
  s->type_name = type_name;
  s->field_count = field_count;
  for (int i = 0; i < field_count; i++) s->fields[i] = 0;
  return VM_OBJ(s);
}

// Allocate + bulk-fill from a contiguous int64 array. The AOT side has
// the stack alloca whose layout matches `ObjStruct::fields`, so the
// promotion site can pass &alloca + memcpy through this single helper.
extern "C" VMValue aot_struct_alloc_from_fields(const char *type_name,
                                                int field_count,
                                                const int64_t *src) {
  VMValue v = aot_struct_alloc(type_name, field_count);
  if (!IS_STRUCT(v) || !src) return v;
  ObjStruct *s = AS_STRUCT(v);
  for (int i = 0; i < field_count; i++) s->fields[i] = src[i];
  return v;
}

// Read field `idx` from a heap struct. Returns 0 on type/range errors so
// callers don't crash on a misuse — typeinfer + AOT type check should
// already prevent the bad cases.
//
// Pointer ABI: VMValue is 16 bytes which the LLVM-C / Windows x64 ABI
// passes via implicit indirection in some configurations. Taking a
// pointer here keeps the call shape stable across platforms — the
// caller alloca-stores the boxed value and hands us the pointer.
extern "C" long long aot_struct_get_field_ptr(VMValue *vp, int idx) {
  if (!vp) return 0;
  VMValue v = *vp;
  if (!IS_STRUCT(v)) return 0;
  ObjStruct *s = AS_STRUCT(v);
  if (idx < 0 || idx >= s->field_count) return 0;
  return s->fields[idx];
}

extern "C" void aot_struct_set_field_ptr(VMValue *vp, int idx, long long val) {
  if (!vp) return;
  VMValue v = *vp;
  if (!IS_STRUCT(v)) return;
  ObjStruct *s = AS_STRUCT(v);
  if (idx < 0 || idx >= s->field_count) return;
  s->fields[idx] = val;
}

// 1 if *vp is a heap struct whose type_name equals `name`. Used by the
// `TypeName{...}` match pattern to discriminate struct variants at runtime.
extern "C" long long aot_struct_type_is_ptr(VMValue *vp, const char *name) {
  if (!vp || !name || !IS_STRUCT(*vp)) return 0;
  ObjStruct *s = AS_STRUCT(*vp);
  return (s->type_name && strcmp(s->type_name, name) == 0) ? 1 : 0;
}

// Unpack heap struct fields into a caller-supplied i64 buffer. Used when
// a typed-struct VAR_DECL takes its RHS from a generic VMValue source
// (`V3 p = points[0];`). The destination matches the stack-alloca
// layout of trivially-unboxable structs, so subsequent `.x`/`.y`
// reads use the existing GEP+load path with zero changes.
//
// `field_count` is the declared struct's field count; we copy
// min(field_count, src->field_count) slots and zero the tail. Mismatch
// is silently truncated/padded — typeinfer is the right place to flag
// it. If `v` isn't a struct, dst is zeroed (matches the "missing
// field returns 0" convention used elsewhere in the codebase).
extern "C" void aot_struct_unpack_to(VMValue *vp, int field_count,
                                     long long *dst) {
  if (!dst || field_count <= 0) return;
  if (vp && IS_STRUCT(*vp)) {
    ObjStruct *s = AS_STRUCT(*vp);
    int n = field_count < s->field_count ? field_count : s->field_count;
    for (int i = 0; i < n; i++) dst[i] = s->fields[i];
    for (int i = n; i < field_count; i++) dst[i] = 0;
  } else {
    for (int i = 0; i < field_count; i++) dst[i] = 0;
  }
}

// ============================================================================
// JSON Serialization - Optimized for Performance
// ============================================================================

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} JSBuilder;

// Thread-local storage
#define TULPAR_TLS TULPAR_THREAD_LOCAL

// Pre-allocated buffer for small JSON (avoids malloc for simple cases)
static TULPAR_TLS char js_small_buffer[4096];
static TULPAR_TLS int js_small_buffer_in_use = 0;

static void js_init(JSBuilder *b) {
  // Use thread-local buffer if available (avoids malloc)
  if (!js_small_buffer_in_use) {
    js_small_buffer_in_use = 1;
    b->data = js_small_buffer;
    b->cap = sizeof(js_small_buffer);
    b->len = 0;
    b->data[0] = '\0';
  } else {
    b->cap = 1024;
    b->len = 0;
    b->data = static_cast<char*>(malloc(b->cap));
    if (b->data)
      b->data[0] = '\0';
  }
}

static void js_free(JSBuilder *b) {
  if (b->data == js_small_buffer) {
    js_small_buffer_in_use = 0;
  } else if (b->data) {
    free(b->data);
  }
  b->data = nullptr;
}

// Ensure capacity - inline for speed
static inline void js_ensure(JSBuilder *b, size_t extra) {
  if (b->len + extra + 1 >= b->cap) {
    size_t new_cap = (b->cap + extra + 256) * 2;
    if (b->data == js_small_buffer) {
      // Migrate from small buffer to heap
      char *new_data = static_cast<char*>(malloc(new_cap));
      if (new_data) {
        memcpy(new_data, b->data, b->len + 1);
        b->data = new_data;
        b->cap = new_cap;
      }
      js_small_buffer_in_use = 0;
    } else {
      b->data = static_cast<char*>(realloc(b->data, new_cap));
      b->cap = new_cap;
    }
  }
}

// Append single character - ultra fast path
static inline void js_append_char(JSBuilder *b, char c) {
  js_ensure(b, 1);
  if (b->data) {
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
  }
}

// Append known-length string - no strlen needed
static inline void js_append_n(JSBuilder *b, const char *str, size_t len) {
  js_ensure(b, len);
  if (b->data) {
    memcpy(b->data + b->len, str, len);
    b->len += len;
    b->data[b->len] = '\0';
  }
}

// Append nullptr-terminated string
static void js_append(JSBuilder *b, const char *str) {
  js_append_n(b, str, strlen(str));
}

// Quick check if string needs escaping (common case: no escaping needed)
static inline int js_needs_escape(const char *str, int len) {
  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    if (c < 32 || c == '"' || c == '\\')
      return 1;
  }
  return 0;
}

// Fast path for simple strings (no escaping needed)
static inline void js_append_simple_string(JSBuilder *b, const char *str,
                                           int len) {
  js_ensure(b, len + 2);
  if (b->data) {
    b->data[b->len++] = '"';
    memcpy(b->data + b->len, str, len);
    b->len += len;
    b->data[b->len++] = '"';
    b->data[b->len] = '\0';
  }
}

// Optimized string escaping - batch copy non-escape chars
static void js_escape_string(JSBuilder *b, const char *str, int len) {
  // Fast path: most strings don't need escaping (especially keys)
  if (!js_needs_escape(str, len)) {
    js_append_simple_string(b, str, len);
    return;
  }

  js_append_char(b, '"');

  // Fast path: find runs of non-escape characters
  int start = 0;
  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    const char *escape = nullptr;
    int escape_len = 0;

    // Check if char needs escaping
    switch (c) {
    case '"':
      escape = "\\\"";
      escape_len = 2;
      break;
    case '\\':
      escape = "\\\\";
      escape_len = 2;
      break;
    case '\b':
      escape = "\\b";
      escape_len = 2;
      break;
    case '\f':
      escape = "\\f";
      escape_len = 2;
      break;
    case '\n':
      escape = "\\n";
      escape_len = 2;
      break;
    case '\r':
      escape = "\\r";
      escape_len = 2;
      break;
    case '\t':
      escape = "\\t";
      escape_len = 2;
      break;
    default:
      if (c < 32) {
        // Flush pending chars first
        if (i > start) {
          js_append_n(b, str + start, i - start);
        }
        char hex[7];
        snprintf(hex, sizeof(hex), "\\u%04x", c);
        js_append_n(b, hex, 6);
        start = i + 1;
      }
      continue; // No escape needed for this char
    }

    // Flush pending non-escaped chars
    if (i > start) {
      js_append_n(b, str + start, i - start);
    }
    js_append_n(b, escape, escape_len);
    start = i + 1;
  }

  // Flush remaining chars
  if (len > start) {
    js_append_n(b, str + start, len - start);
  }

  js_append_char(b, '"');
}

// Fast integer to string conversion (faster than snprintf)
static inline int js_int_to_str(char *buf, long long val) {
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return 1;
  }

  char tmp[24];
  int i = 0;
  int neg = 0;
  unsigned long long uval;

  if (val < 0) {
    neg = 1;
    uval = (unsigned long long)(-val);
  } else {
    uval = (unsigned long long)val;
  }

  while (uval > 0) {
    tmp[i++] = '0' + (uval % 10);
    uval /= 10;
  }

  int len = 0;
  if (neg)
    buf[len++] = '-';
  while (i > 0)
    buf[len++] = tmp[--i];
  buf[len] = '\0';
  return len;
}

static void js_serialize(JSBuilder *b, VMValue v, int depth) {
  if (depth > 20) {
    js_append_n(b, "\"<max_depth>\"", 13);
    return;
  }

  if (IS_INT(v)) {
    char tmp[24];
    int len = js_int_to_str(tmp, AS_INT(v));
    js_append_n(b, tmp, len);
  } else if (IS_FLOAT(v)) {
    char tmp[64];
    int len = snprintf(tmp, sizeof(tmp), "%g", AS_FLOAT(v));
    js_append_n(b, tmp, len);
  } else if (IS_BOOL(v)) {
    if (AS_BOOL(v)) {
      js_append_n(b, "true", 4);
    } else {
      js_append_n(b, "false", 5);
    }
  } else if (IS_STRING(v)) {
    ObjString *s = AS_STRING(v);
    js_escape_string(b, s->chars, s->length);
  } else if (IS_ARRAY(v)) {
    ObjArray *arr = AS_ARRAY(v);
    js_append_char(b, '[');
    for (int i = 0; i < arr->count; i++) {
      if (i > 0)
        js_append_char(b, ',');
      js_serialize(b, arr->items[i], depth + 1);
    }
    js_append_char(b, ']');
  } else if (IS_OBJECT(v)) {
    ObjObject *obj = AS_OBJECT(v);
    js_append_char(b, '{');
    for (int i = 0; i < obj->count; i++) {
      if (i > 0)
        js_append_char(b, ',');
      js_escape_string(b, obj->keys[i]->chars, obj->keys[i]->length);
      js_append_char(b, ':');
      js_serialize(b, obj->values[i], depth + 1);
    }
    js_append_char(b, '}');
  } else {
    js_append_n(b, "nullptr", 4);
  }
}

// ============================================================
// cJSON-based Fast JSON Serializer
// ============================================================

// Convert VMValue to cJSON object
static cJSON *vmvalue_to_cjson(VMValue v, int depth) {
  if (depth > 20)
    return cJSON_CreateString("<max_depth>");

  if (IS_INT(v)) {
    return cJSON_CreateNumber((double)AS_INT(v));
  } else if (IS_FLOAT(v)) {
    return cJSON_CreateNumber(AS_FLOAT(v));
  } else if (IS_BOOL(v)) {
    return cJSON_CreateBool(AS_BOOL(v));
  } else if (IS_STRING(v)) {
    ObjString *s = AS_STRING(v);
    return cJSON_CreateString(s->chars);
  } else if (IS_ARRAY(v)) {
    ObjArray *arr = AS_ARRAY(v);
    cJSON *json_arr = cJSON_CreateArray();
    for (int i = 0; i < arr->count; i++) {
      cJSON_AddItemToArray(json_arr,
                           vmvalue_to_cjson(arr->items[i], depth + 1));
    }
    return json_arr;
  } else if (IS_OBJECT(v)) {
    ObjObject *obj = AS_OBJECT(v);
    cJSON *json_obj = cJSON_CreateObject();
    for (int i = 0; i < obj->count; i++) {
      cJSON_AddItemToObject(json_obj, obj->keys[i]->chars,
                            vmvalue_to_cjson(obj->values[i], depth + 1));
    }
    return json_obj;
  }
  return cJSON_CreateNull();
}

// toJson using cJSON (faster than manual implementation)
VMValue aot_to_json_cjson(VMValue value) {
  cJSON *json = vmvalue_to_cjson(value, 0);
  if (!json)
    return VM_INT(0);

  // Use unformatted print for speed (no pretty printing)
  char *str = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);

  if (!str)
    return VM_INT(0);

  int len = strlen(str);
  ObjString *res = aot_allocate_string(str, len);
  free(str); // cJSON allocates with malloc

  return VM_OBJ((Obj *)res);
}

// toJson(VMValue) -> VMValue (String)
// Uses our optimized JSBuilder implementation
VMValue aot_to_json(VMValue value) {
  JSBuilder b;
  js_init(&b);
  if (!b.data)
    return VM_INT(0); // Alloc failed

  js_serialize(&b, value, 0);

  ObjString *res = aot_allocate_string(b.data, b.len);
  js_free(&b);
  return VM_OBJ((Obj *)res);
}

VMValue aot_to_json_ptr(VMValue *value_ptr) {
  if (!value_ptr)
    return VM_INT(0);
  return aot_to_json(*value_ptr);
}

// AOT Wrappers for Allocations
ObjArray *vm_allocate_array_aot_wrapper(void *vm) {
  if (vm)
    return vm_allocate_array(static_cast<VM *>(vm));
  ObjArray *arr = static_cast<ObjArray*>(malloc(sizeof(ObjArray)));
  arr->obj.type = OBJ_ARRAY;
  arr->obj.arena_allocated = 0;
  arr->obj.next = nullptr;
  arr->obj.ref_count = 1;
  arr->obj.is_moved = 0;
  arr->count = 0;
  arr->capacity = 0;
  arr->items = nullptr;
  region_track((Obj *)arr); // request-local literal → freed at arena_restore
  return arr;
}

ObjObject *vm_allocate_object_aot_wrapper(void *vm) {
  if (vm)
    return vm_allocate_object(static_cast<VM *>(vm));
  ObjObject *obj = static_cast<ObjObject*>(malloc(sizeof(ObjObject)));
  obj->obj.type = OBJ_OBJECT;
  obj->obj.arena_allocated = 0;
  obj->obj.next = nullptr;
  obj->obj.ref_count = 1;
  obj->obj.is_moved = 0;
  obj->count = 0;
  obj->capacity = 0;
  obj->keys = nullptr;
  obj->values = nullptr;
  region_track((Obj *)obj); // request-local literal → freed at arena_restore
  return obj;
}

void vm_array_push_aot_wrapper(void *vm, ObjArray *array, VMValue value) {
  if (vm) {
    // Since we don't have vm_array_push_wrapper symbol available (it's
    // vm_array_push) We call vm_array_push. Assuming vm_array_push is available
    // (linked from vm.c).
    vm_array_push(static_cast<VM *>(vm), array, value);
    return;
  }
  if (array->count >= array->capacity) {
    int old_capacity = array->capacity;
    int new_cap = old_capacity < 8 ? 8 : old_capacity * 2;
    if (array->obj.arena_allocated) {
      VMValue *new_items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
      if (old_capacity > 0) {
        memcpy(new_items, array->items, sizeof(VMValue) * array->count);
      }
      array->items = new_items;
    } else {
      array->items = static_cast<VMValue*>(realloc(array->items, sizeof(VMValue) * new_cap));
    }
    array->capacity = new_cap;
  }
  array->items[array->count++] = value;
}

void vm_array_push_aot_ptr_wrapper(void *vm, ObjArray *array,
                                   VMValue *value_ptr) {
  if (!value_ptr)
    return;
  vm_array_push_aot_wrapper(vm, array, *value_ptr);
}

void vm_object_set_aot_wrapper(void *vm, ObjObject *obj, char *key,
                               VMValue value) {
  if (vm) {
    vm_object_set(static_cast<VM *>(vm), obj, key, value);
    return;
  }
  // AOT Logic
  if (obj->count >= obj->capacity) {
    int old_capacity = obj->capacity;
    int new_cap = old_capacity < 8 ? 8 : old_capacity * 2;
    if (obj->obj.arena_allocated) {
      ObjString **new_keys = (ObjString **)aot_arena_alloc(sizeof(ObjString *) * new_cap);
      VMValue *new_values = (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
      if (old_capacity > 0) {
        memcpy(new_keys, obj->keys, sizeof(ObjString *) * obj->count);
        memcpy(new_values, obj->values, sizeof(VMValue) * obj->count);
      }
      obj->keys = new_keys;
      obj->values = new_values;
    } else {
      obj->keys = (ObjString **)realloc(obj->keys, sizeof(ObjString *) * new_cap);
      obj->values = static_cast<VMValue*>(realloc(obj->values, sizeof(VMValue) * new_cap));
    }
    obj->capacity = new_cap;
  }

  // Create string object for key.
  // aot_allocate_string copies the chars.
  obj->keys[obj->count] = aot_allocate_string(key, strlen(key));
  obj->values[obj->count] = value;
  obj->count++;
}

void vm_object_set_aot_ptr_wrapper(void *vm, ObjObject *obj, char *key,
                                   VMValue *value_ptr) {
  if (!value_ptr)
    return;
  vm_object_set_aot_wrapper(vm, obj, key, *value_ptr);
}

// --- AOT Builtin Logic for Input and Strings ---

// AOT Builtin: now_iso8601() -> str
//
// Returns the current UTC time as a 20-byte ISO 8601 string
// `YYYY-MM-DDTHH:MM:SSZ`. Common API need (request timestamps, log
// lines) and trivial to do in C without dragging in user-space
// strftime calls every request.
VMValue aot_now_iso8601(void) {
  time_t t = time(nullptr);
#if defined(_WIN32)
  struct tm tm;
  gmtime_s(&tm, &t);
  struct tm *tmp = &tm;
#else
  struct tm tmbuf;
  struct tm *tmp = gmtime_r(&t, &tmbuf);
#endif
  char buf[24];
  if (!tmp) {
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  }
  int n = (int)strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tmp);
  return VM_OBJ((Obj *)aot_allocate_string(buf, n));
}

// ---------------------------------------------------------------------------
// Regex builtins (ECMAScript syntax, std::regex).
//
// Surface:
//   regex_match(pattern, str)   -> 1 if full-string match else 0
//   regex_search(pattern, str)  -> 1 if substring match else 0
//   regex_capture(pattern, str) -> array of capture groups
//                                  ([] when no match; [whole, g1, g2, ...] otherwise)
//   regex_replace(pattern, str, replacement) -> new string with all matches
//                                  replaced (replacement supports `$1` etc.)
//
// Patterns are compiled per-call. A compiled-regex cache is on the
// roadmap; benchmark first.
// ---------------------------------------------------------------------------

namespace {
// Helper: compile or return null on failure (so callers can early-out
// without exceptions reaching Tulpar).
std::regex *try_compile_regex(const char *pattern) {
    try {
        return new std::regex(pattern, std::regex::ECMAScript);
    } catch (...) {
        return nullptr;
    }
}
}  // namespace

VMValue aot_regex_match(VMValue patVal, VMValue strVal) {
    if (!IS_STRING(patVal) || !IS_STRING(strVal)) return VM_INT(0);
    std::regex *re = try_compile_regex(AS_STRING(patVal)->chars);
    if (!re) return VM_INT(0);
    bool ok;
    try {
        ok = std::regex_match(AS_STRING(strVal)->chars, *re);
    } catch (...) {
        ok = false;
    }
    delete re;
    return VM_INT(ok ? 1 : 0);
}

VMValue aot_regex_search(VMValue patVal, VMValue strVal) {
    if (!IS_STRING(patVal) || !IS_STRING(strVal)) return VM_INT(0);
    std::regex *re = try_compile_regex(AS_STRING(patVal)->chars);
    if (!re) return VM_INT(0);
    bool ok;
    try {
        ok = std::regex_search(AS_STRING(strVal)->chars, *re);
    } catch (...) {
        ok = false;
    }
    delete re;
    return VM_INT(ok ? 1 : 0);
}

VMValue aot_regex_capture(VMValue patVal, VMValue strVal) {
    // Empty array on failure / no-match.
    auto empty = []() -> VMValue {
        ObjArray *a = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
        a->obj.type = OBJ_ARRAY;
        a->obj.arena_allocated = 1;
        a->obj.next = nullptr;
        a->obj.ref_count = 1;
        a->obj.is_moved = 0;
        a->capacity = 0;
        a->count = 0;
        a->items = nullptr;
        return VM_OBJ((Obj *)a);
    };
    if (!IS_STRING(patVal) || !IS_STRING(strVal)) return empty();
    std::regex *re = try_compile_regex(AS_STRING(patVal)->chars);
    if (!re) return empty();
    std::cmatch m;
    bool ok = false;
    try {
        ok = std::regex_search(AS_STRING(strVal)->chars, m, *re);
    } catch (...) {
        ok = false;
    }
    delete re;
    if (!ok) return empty();
    int n = (int)m.size();
    ObjArray *a = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
    a->obj.type = OBJ_ARRAY;
    a->obj.arena_allocated = 1;
    a->obj.next = nullptr;
    a->obj.ref_count = 1;
    a->obj.is_moved = 0;
    a->capacity = n;
    a->count = n;
    a->items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * n);
    for (int i = 0; i < n; i++) {
        std::string s = m[i].str();
        a->items[i] = VM_OBJ((Obj *)aot_allocate_string(s.data(), (int)s.size()));
    }
    return VM_OBJ((Obj *)a);
}

VMValue aot_regex_replace(VMValue patVal, VMValue strVal, VMValue replVal) {
    if (!IS_STRING(patVal) || !IS_STRING(strVal) || !IS_STRING(replVal)) {
        return strVal;  // pass through on bad args
    }
    std::regex *re = try_compile_regex(AS_STRING(patVal)->chars);
    if (!re) return strVal;
    std::string out;
    try {
        out = std::regex_replace(std::string(AS_STRING(strVal)->chars,
                                              AS_STRING(strVal)->length),
                                  *re, AS_STRING(replVal)->chars);
    } catch (...) {
        delete re;
        return strVal;
    }
    delete re;
    return VM_OBJ((Obj *)aot_allocate_string(out.data(), (int)out.size()));
}

// AOT Builtin: parse_iso8601(str) -> int (unix seconds, -1 on failure)
//
// Accepts the canonical `YYYY-MM-DDTHH:MM:SSZ` Z-form that
// `now_iso8601` / `format_iso8601` produce, plus the lenient
// `YYYY-MM-DD HH:MM:SS` (space separator, no Z) for SQLite-shaped
// timestamps. No timezone offsets, no fractional seconds — those are
// rare in API payloads and would balloon the parser.
VMValue aot_parse_iso8601(VMValue strVal) {
  if (!IS_STRING(strVal)) return VM_INT(-1);
  ObjString *s = AS_STRING(strVal);
  const char *c = s->chars;
  int len = s->length;
  if (len < 19) return VM_INT(-1);

  auto digit = [](char ch) -> int {
    return (ch >= '0' && ch <= '9') ? (ch - '0') : -1;
  };
  auto two = [&](int off) -> int {
    int a = digit(c[off]);
    int b = digit(c[off + 1]);
    if (a < 0 || b < 0) return -1;
    return a * 10 + b;
  };
  auto four = [&](int off) -> int {
    int a = digit(c[off]);
    int b = digit(c[off + 1]);
    int d = digit(c[off + 2]);
    int e = digit(c[off + 3]);
    if (a < 0 || b < 0 || d < 0 || e < 0) return -1;
    return a * 1000 + b * 100 + d * 10 + e;
  };

  int year = four(0);
  if (year < 0 || c[4] != '-') return VM_INT(-1);
  int month = two(5);
  if (month < 1 || month > 12 || c[7] != '-') return VM_INT(-1);
  int day = two(8);
  if (day < 1 || day > 31 || (c[10] != 'T' && c[10] != ' ')) return VM_INT(-1);
  int hour = two(11);
  if (hour < 0 || hour > 23 || c[13] != ':') return VM_INT(-1);
  int min = two(14);
  if (min < 0 || min > 59 || c[16] != ':') return VM_INT(-1);
  int sec = two(17);
  if (sec < 0 || sec > 60) return VM_INT(-1);

  // Convert to UTC unix seconds without depending on `timegm` (not on
  // Windows' standard CRT) — formula adapted from POSIX/Howard Hinnant's
  // days-from-civil. Handles 1970..9999 safely.
  int y = (month <= 2) ? year - 1 : year;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153u * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long long days = (long long)era * 146097 + (long long)doe - 719468;
  long long secs = days * 86400 + (long long)hour * 3600 +
                   (long long)min * 60 + sec;
  return VM_INT(secs);
}

// ---------------------------------------------------------------------------
// File glob: `file_glob(pattern) -> array<str>`
//
// Shell-style: `*` matches any run of chars (no `/`), `?` matches one
// char (no `/`), everything else is literal. Returns an alphabetised
// list of matching paths under the pattern's directory part. Used by
// scripts that want to enumerate `.tpr` files, log files etc.
// ---------------------------------------------------------------------------

namespace {

bool glob_match_segment(const char *pat, int plen, const char *str, int slen) {
    // Two-pointer regex-lite for `*` `?`. Recursive on `*`.
    int pi = 0, si = 0, star = -1, sstar = 0;
    while (si < slen) {
        if (pi < plen && (pat[pi] == str[si] || pat[pi] == '?')) {
            pi++; si++;
        } else if (pi < plen && pat[pi] == '*') {
            star = pi++;
            sstar = si;
        } else if (star != -1) {
            pi = star + 1;
            si = ++sstar;
        } else {
            return false;
        }
    }
    while (pi < plen && pat[pi] == '*') pi++;
    return pi == plen;
}

}  // namespace

VMValue aot_file_glob(VMValue patVal) {
    auto empty = []() -> VMValue {
        ObjArray *a = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
        a->obj.type = OBJ_ARRAY;
        a->obj.arena_allocated = 1;
        a->obj.next = nullptr;
        a->obj.ref_count = 1;
        a->obj.is_moved = 0;
        a->capacity = 0;
        a->count = 0;
        a->items = nullptr;
        return VM_OBJ((Obj *)a);
    };
    if (!IS_STRING(patVal)) return empty();
    const char *pattern = AS_STRING(patVal)->chars;

    // Split into directory + filename pattern (last `/` or `\`).
    std::string dir = ".";
    std::string fpat = pattern;
    size_t slash = fpat.find_last_of("/\\");
    if (slash != std::string::npos) {
        dir = fpat.substr(0, slash);
        if (dir.empty()) dir = "/";
        fpat = fpat.substr(slash + 1);
    }

    std::vector<std::string> matches;
#if defined(_WIN32)
    // Use FindFirstFile via std::filesystem — portable and avoids
    // dragging Win32 API surface into the file.
    try {
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            std::string name = entry.path().filename().string();
            if (glob_match_segment(fpat.data(), (int)fpat.size(),
                                    name.data(), (int)name.size())) {
                matches.push_back(entry.path().string());
            }
        }
    } catch (...) { /* missing dir -> empty match */ }
#else
    try {
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            std::string name = entry.path().filename().string();
            if (glob_match_segment(fpat.data(), (int)fpat.size(),
                                    name.data(), (int)name.size())) {
                matches.push_back(entry.path().string());
            }
        }
    } catch (...) {}
#endif
    std::sort(matches.begin(), matches.end());

    ObjArray *a = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
    a->obj.type = OBJ_ARRAY;
    a->obj.arena_allocated = 1;
    a->obj.next = nullptr;
    a->obj.ref_count = 1;
    a->obj.is_moved = 0;
    int n = (int)matches.size();
    a->capacity = n;
    a->count = n;
    a->items = n ? (VMValue *)aot_arena_alloc(sizeof(VMValue) * n) : nullptr;
    for (int i = 0; i < n; i++) {
        a->items[i] = VM_OBJ(
            (Obj *)aot_allocate_string(matches[i].data(), (int)matches[i].size()));
    }
    return VM_OBJ((Obj *)a);
}

// ---------------------------------------------------------------------------
// CSV: `csv_parse(str) -> array<array<str>>`, `csv_emit(rows) -> str`
//
// RFC 4180 minimal: comma delimiter, `\r\n` row separator on emit
// (parse accepts `\n` too), `"…"` quoted fields with `""` escaping a
// literal quote. No type inference (everything stays a string) — call
// sites can `toInt` / `toFloat` if they need numeric values.
// ---------------------------------------------------------------------------

VMValue aot_csv_parse(VMValue strVal) {
    auto empty = []() -> VMValue {
        ObjArray *a = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
        a->obj.type = OBJ_ARRAY;
        a->obj.arena_allocated = 1;
        a->obj.next = nullptr;
        a->obj.ref_count = 1;
        a->obj.is_moved = 0;
        a->capacity = 0;
        a->count = 0;
        a->items = nullptr;
        return VM_OBJ((Obj *)a);
    };
    if (!IS_STRING(strVal)) return empty();
    const char *s = AS_STRING(strVal)->chars;
    int len = AS_STRING(strVal)->length;

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> cur_row;
    std::string cur_field;
    bool in_quote = false;
    bool field_started = false;

    auto flush_field = [&]() {
        cur_row.push_back(std::move(cur_field));
        cur_field.clear();
        field_started = false;
    };
    auto flush_row = [&]() {
        flush_field();
        rows.push_back(std::move(cur_row));
        cur_row.clear();
    };

    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (in_quote) {
            if (c == '"') {
                if (i + 1 < len && s[i + 1] == '"') {
                    cur_field.push_back('"');
                    i++;
                } else {
                    in_quote = false;
                }
            } else {
                cur_field.push_back(c);
            }
            continue;
        }
        if (c == '"' && !field_started) {
            in_quote = true;
            field_started = true;
            continue;
        }
        if (c == ',') { flush_field(); continue; }
        if (c == '\r') continue;  // tolerate stray CR
        if (c == '\n') { flush_row(); continue; }
        field_started = true;
        cur_field.push_back(c);
    }
    if (!cur_field.empty() || !cur_row.empty()) flush_row();

    int rn = (int)rows.size();
    ObjArray *outer = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
    outer->obj.type = OBJ_ARRAY;
    outer->obj.arena_allocated = 1;
    outer->obj.next = nullptr;
    outer->obj.ref_count = 1;
    outer->obj.is_moved = 0;
    outer->capacity = rn;
    outer->count = rn;
    outer->items = rn ? (VMValue *)aot_arena_alloc(sizeof(VMValue) * rn) : nullptr;
    for (int r = 0; r < rn; r++) {
        int cn = (int)rows[r].size();
        ObjArray *inner = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
        inner->obj.type = OBJ_ARRAY;
        inner->obj.arena_allocated = 1;
        inner->obj.next = nullptr;
        inner->obj.ref_count = 1;
        inner->obj.is_moved = 0;
        inner->capacity = cn;
        inner->count = cn;
        inner->items = cn ? (VMValue *)aot_arena_alloc(sizeof(VMValue) * cn) : nullptr;
        for (int c = 0; c < cn; c++) {
            inner->items[c] = VM_OBJ(
                (Obj *)aot_allocate_string(rows[r][c].data(), (int)rows[r][c].size()));
        }
        outer->items[r] = VM_OBJ((Obj *)inner);
    }
    return VM_OBJ((Obj *)outer);
}

VMValue aot_csv_emit(VMValue rowsVal) {
    if (!IS_ARRAY(rowsVal)) return VM_OBJ((Obj *)aot_allocate_string("", 0));
    ObjArray *rows = (ObjArray *)AS_OBJECT(rowsVal);
    std::string out;
    out.reserve(rows->count * 32);

    auto needs_quote = [](const char *s, int n) {
        for (int i = 0; i < n; i++) {
            char c = s[i];
            if (c == ',' || c == '"' || c == '\n' || c == '\r') return true;
        }
        return false;
    };

    for (int r = 0; r < rows->count; r++) {
        VMValue rv = rows->items[r];
        if (!IS_ARRAY(rv)) continue;
        ObjArray *row = (ObjArray *)AS_OBJECT(rv);
        for (int c = 0; c < row->count; c++) {
            if (c > 0) out.push_back(',');
            VMValue fv = row->items[c];
            if (!IS_STRING(fv)) continue;
            ObjString *fs = AS_STRING(fv);
            if (needs_quote(fs->chars, fs->length)) {
                out.push_back('"');
                for (int k = 0; k < fs->length; k++) {
                    if (fs->chars[k] == '"') out.push_back('"');
                    out.push_back(fs->chars[k]);
                }
                out.push_back('"');
            } else {
                out.append(fs->chars, fs->length);
            }
        }
        out.append("\r\n");
    }
    return VM_OBJ((Obj *)aot_allocate_string(out.data(), (int)out.size()));
}

// AOT Builtin: keys(obj) -> array<str>
//
// Returns the keys of a JSON-like object in their original insertion
// order. The returned array shares no string objects with the original
// (each key is re-allocated in the arena), so the caller may keep the
// result around even after the source object is mutated.
VMValue aot_keys(VMValue objVal) {
    auto empty = []() -> VMValue {
        ObjArray *a = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
        a->obj.type = OBJ_ARRAY;
        a->obj.arena_allocated = 1;
        a->obj.next = nullptr;
        a->obj.ref_count = 1;
        a->obj.is_moved = 0;
        a->capacity = 0;
        a->count = 0;
        a->items = nullptr;
        return VM_OBJ((Obj *)a);
    };
    if (!IS_OBJECT(objVal)) return empty();
    ObjObject *o = (ObjObject *)AS_OBJECT(objVal);
    int n = o->count;
    ObjArray *a = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
    a->obj.type = OBJ_ARRAY;
    a->obj.arena_allocated = 1;
    a->obj.next = nullptr;
    a->obj.ref_count = 1;
    a->obj.is_moved = 0;
    a->capacity = n;
    a->count = n;
    a->items = n ? (VMValue *)aot_arena_alloc(sizeof(VMValue) * n) : nullptr;
    for (int i = 0; i < n; i++) {
        ObjString *k = o->keys[i];
        if (!k) {
            a->items[i] = VM_OBJ((Obj *)aot_allocate_string("", 0));
        } else {
            a->items[i] = VM_OBJ(
                (Obj *)aot_allocate_string(k->chars, k->length));
        }
    }
    return VM_OBJ((Obj *)a);
}

// AOT Builtin: clone(obj) -> obj  (shallow copy of a JSON object)
//
// Allocates a fresh ObjObject and copies the keys + value VMValues
// from the source. Strings are NOT re-allocated (the new keys array
// holds the same ObjString pointers — they're immutable). Nested
// objects / arrays remain shared by reference, so this is a shallow
// clone — adequate for primitive-field structs (`Point { int x; int
// y; }` etc.) which is the by-value-param use case driving this
// helper. Non-object inputs pass through unchanged (lets the VM
// emit-once "clone every typed-struct param" prologue stay
// conservative — if the caller passed an int, clone just returns
// the int).
VMValue aot_object_clone(VMValue val) {
  if (!IS_OBJECT(val)) return val;
  ObjObject *src = (ObjObject *)AS_OBJECT(val);
  ObjObject *dst = (ObjObject *)aot_arena_alloc(sizeof(ObjObject));
  dst->obj.type = OBJ_OBJECT;
  dst->obj.arena_allocated = 1;
  dst->obj.next = nullptr;
  dst->obj.ref_count = 1;
  dst->obj.is_moved = 0;
  int n = src->count;
  dst->count = n;
  dst->capacity = n;
  if (n > 0) {
    dst->keys = (ObjString **)aot_arena_alloc(sizeof(ObjString *) * n);
    dst->values = (VMValue *)aot_arena_alloc(sizeof(VMValue) * n);
    for (int i = 0; i < n; i++) {
      dst->keys[i] = src->keys[i];
      dst->values[i] = src->values[i];
    }
  } else {
    dst->keys = nullptr;
    dst->values = nullptr;
  }
  return VM_OBJ((Obj *)dst);
}

// AOT Builtin: weekday(unix_seconds) -> int  (0=Sunday … 6=Saturday)
//
// Matches the POSIX `tm_wday` convention. Most apps want this for
// scheduling ("only run on Monday") and for friendly date rendering.
VMValue aot_weekday(VMValue secsVal) {
  time_t t;
  if (IS_INT(secsVal))        t = (time_t)AS_INT(secsVal);
  else if (IS_FLOAT(secsVal)) t = (time_t)AS_FLOAT(secsVal);
  else                         return VM_INT(-1);
#if defined(_WIN32)
  struct tm tm; gmtime_s(&tm, &t);
  return VM_INT(tm.tm_wday);
#else
  struct tm tm; gmtime_r(&t, &tm);
  return VM_INT(tm.tm_wday);
#endif
}

// AOT Builtin: date_add_seconds(unix_seconds, delta) -> int
//
// Trivially `a + b`; exposed as a builtin so user code reads as
// `t = date_add_seconds(now, 3600)` instead of an opaque integer add
// — and so we have a stable name when we later add date-aware
// arithmetic (e.g. add_months that rolls month boundaries).
VMValue aot_date_add_seconds(VMValue baseVal, VMValue deltaVal) {
  long long base = 0, delta = 0;
  if (IS_INT(baseVal))        base = AS_INT(baseVal);
  else if (IS_FLOAT(baseVal)) base = (long long)AS_FLOAT(baseVal);
  if (IS_INT(deltaVal))        delta = AS_INT(deltaVal);
  else if (IS_FLOAT(deltaVal)) delta = (long long)AS_FLOAT(deltaVal);
  return VM_INT(base + delta);
}

// AOT Builtin: format_iso8601(unix_seconds) -> str
VMValue aot_format_iso8601(VMValue secsVal) {
  time_t t;
  if (IS_INT(secsVal))         t = (time_t)AS_INT(secsVal);
  else if (IS_FLOAT(secsVal))  t = (time_t)AS_FLOAT(secsVal);
  else                          return VM_OBJ((Obj *)aot_allocate_string("", 0));
#if defined(_WIN32)
  struct tm tm;
  gmtime_s(&tm, &t);
  struct tm *tmp = &tm;
#else
  struct tm tmbuf;
  struct tm *tmp = gmtime_r(&t, &tmbuf);
#endif
  char buf[24];
  if (!tmp) return VM_OBJ((Obj *)aot_allocate_string("", 0));
  int n = (int)strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tmp);
  return VM_OBJ((Obj *)aot_allocate_string(buf, n));
}

// AOT Builtin: env(name) -> str
// Reads a process environment variable. Returns "" when unset, so user
// code can do `if (env("DEBUG") != "") { ... }` without a separate
// "is set" predicate. Used by stdlib to gate behavior (HTTP access logs,
// debug traces) without baking switches into the runtime.
VMValue aot_env(VMValue nameVal) {
  if (!IS_STRING(nameVal)) {
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  }
  const char *name = AS_STRING(nameVal)->chars;
  const char *val = std::getenv(name);
  if (!val) {
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  }
  return VM_OBJ((Obj *)aot_allocate_string(val, (int)strlen(val)));
}

// AOT Input: Reads a line from stdin
VMValue aot_input() {
  char buffer[1024];
  if (fgets(buffer, sizeof(buffer), stdin)) {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
      len--;
    }
    if (len > 0 && buffer[len - 1] == '\r') {
      buffer[len - 1] = '\0';
      len--;
    }
    ObjString *str = aot_allocate_string(buffer, (int)len);
    return VM_OBJ((Obj *)str);
  }
  ObjString *empty = aot_allocate_string("", 0);
  return VM_OBJ((Obj *)empty);
}

// AOT Trim: Removes whitespace from start/end
VMValue aot_trim(VMValue val) {
  if (!IS_STRING(val))
    return val;
  ObjString *s = AS_STRING(val);

  int start = 0;
  while (start < s->length &&
         (s->chars[start] == ' ' || s->chars[start] == '\t' ||
          s->chars[start] == '\n' || s->chars[start] == '\r')) {
    start++;
  }

  int end = s->length - 1;
  while (end > start && (s->chars[end] == ' ' || s->chars[end] == '\t' ||
                         s->chars[end] == '\n' || s->chars[end] == '\r')) {
    end--;
  }

  int new_len = end - start + 1;
  if (new_len < 0)
    new_len = 0;

  char *buf = static_cast<char*>(malloc(new_len + 1));
  if (new_len > 0) {
    memcpy(buf, s->chars + start, new_len);
  }
  buf[new_len] = '\0';

  ObjString *res = aot_allocate_string(buf, new_len);
  free(buf);
  return VM_OBJ((Obj *)res);
}

VMValue aot_trim_ptr(VMValue *val_ptr) {
  if (!val_ptr)
    return VM_INT(0);
  return aot_trim(*val_ptr);
}

// AOT Replace
VMValue aot_replace(VMValue strVal, VMValue oldVal, VMValue newVal) {
  if (!IS_STRING(strVal) || !IS_STRING(oldVal) || !IS_STRING(newVal))
    return strVal;

  ObjString *s = AS_STRING(strVal);
  ObjString *oldS = AS_STRING(oldVal);
  ObjString *newS = AS_STRING(newVal);

  if (oldS->length == 0)
    return strVal;

  int count = 0;
  const char *tmp = s->chars;
  while ((tmp = strstr(tmp, oldS->chars))) {
    count++;
    tmp += oldS->length;
  }

  int new_len = s->length + count * (newS->length - oldS->length);
  char *result = static_cast<char*>(malloc(new_len + 1));

  char *ptr = result;
  const char *src = s->chars;
  const char *found;

  while ((found = strstr(src, oldS->chars))) {
    int segment_len = (int)(found - src);
    memcpy(ptr, src, segment_len);
    ptr += segment_len;

    memcpy(ptr, newS->chars, newS->length);
    ptr += newS->length;

    src = found + oldS->length;
  }
  strcpy(ptr, src);

  ObjString *resObj = aot_allocate_string(result, new_len);
  free(result);
  return VM_OBJ((Obj *)resObj);
}

VMValue aot_replace_ptr(VMValue *str_ptr, VMValue *old_ptr, VMValue *new_ptr) {
  if (!str_ptr || !old_ptr || !new_ptr)
    return VM_INT(0);
  return aot_replace(*str_ptr, *old_ptr, *new_ptr);
}

// AOT Split
VMValue aot_split(VMValue strVal, VMValue delVal) {
  if (!IS_STRING(strVal) || !IS_STRING(delVal)) {
    ObjArray *arr = vm_allocate_array_aot_wrapper(nullptr);
    return VM_OBJ((Obj *)arr);
  }

  ObjString *s = AS_STRING(strVal);
  ObjString *d = AS_STRING(delVal);

  ObjArray *arr = vm_allocate_array_aot_wrapper(nullptr);

  if (s->length == 0)
    return VM_OBJ((Obj *)arr);

  if (d->length == 0) {
    for (int i = 0; i < s->length; i++) {
      char single[2] = {s->chars[i], '\0'};
      ObjString *seg = aot_allocate_string(single, 1);
      vm_array_push_aot_wrapper(nullptr, arr, VM_OBJ((Obj *)seg));
    }
  } else {
    const char *start = s->chars;
    const char *p;
    while ((p = strstr(start, d->chars)) != nullptr) {
      int len = (int)(p - start);
      char *sub = static_cast<char*>(malloc(len + 1));
      strncpy(sub, start, len);
      sub[len] = '\0';

      ObjString *seg = aot_allocate_string(sub, len);
      vm_array_push_aot_wrapper(nullptr, arr, VM_OBJ((Obj *)seg));
      free(sub);

      start = p + d->length;
    }
    ObjString *seg = aot_allocate_string(start, strlen(start));
    vm_array_push_aot_wrapper(nullptr, arr, VM_OBJ((Obj *)seg));
  }

  return VM_OBJ((Obj *)arr);
}

VMValue aot_split_ptr(VMValue *str_ptr, VMValue *del_ptr) {
  if (!str_ptr || !del_ptr)
    return VM_INT(0);
  return aot_split(*str_ptr, *del_ptr);
}

// Return a new array holding elements [start, end) of *arr_ptr. Used by the
// `[head, ..rest]` match destructuring pattern to bind the tail. A non-array
// input or out-of-range start yields an empty array (matches the codebase's
// "misuse is silent" convention).
VMValue aot_array_slice_ptr(VMValue *arr_ptr, long long start) {
  ObjArray *out = vm_allocate_array_aot_wrapper(nullptr);
  if (arr_ptr && IS_ARRAY(*arr_ptr)) {
    ObjArray *src = AS_ARRAY(*arr_ptr);
    if (start < 0) start = 0;
    for (int i = (int)start; i < src->count; i++)
      vm_array_push_aot_wrapper(nullptr, out, src->items[i]);
  }
  return VM_OBJ((Obj *)out);
}
// ============================================================================
// File I/O Builtins
// ============================================================================

VMValue aot_read_file(VMValue path_val) {
  if (!IS_STRING(path_val))
    return VM_VOID();
  const char *path = AS_STRING(path_val)->chars;

  FILE *f = fopen(path, "rb");
  if (!f)
    return VM_VOID();

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *string = static_cast<char*>(malloc(fsize + 1));
  if (string) {
    size_t result = fread(string, 1, fsize, f);
    string[fsize] = 0;
    if (result != (size_t)fsize) {
      // Read failed or partial
    }
  }
  fclose(f);

  if (!string)
    return VM_VOID();

  // Create ObjString (copies char*)
  ObjString *ostr = vm_alloc_string_aot(nullptr, string, (int)fsize);
  free(string);

  return VM_OBJ(ostr);
}

VMValue aot_read_file_ptr(VMValue *path_ptr) {
  if (!path_ptr)
    return VM_VOID();
  return aot_read_file(*path_ptr);
}

VMValue aot_write_file(VMValue path_val, VMValue content_val) {
  if (!IS_STRING(path_val) || !IS_STRING(content_val))
    return VM_VOID();
  const char *path = AS_STRING(path_val)->chars;
  const char *content = AS_STRING(content_val)->chars;
  int len = AS_STRING(content_val)->length;

  FILE *f = fopen(path, "wb");
  if (f) {
    fwrite(content, 1, len, f);
    fclose(f);
  }
  return VM_VOID();
}

VMValue aot_write_file_ptr(VMValue *path_ptr, VMValue *content_ptr) {
  if (!path_ptr || !content_ptr)
    return VM_VOID();
  return aot_write_file(*path_ptr, *content_ptr);
}

VMValue aot_append_file(VMValue path_val, VMValue content_val) {
  if (!IS_STRING(path_val) || !IS_STRING(content_val))
    return VM_VOID();
  const char *path = AS_STRING(path_val)->chars;
  const char *content = AS_STRING(content_val)->chars;
  int len = AS_STRING(content_val)->length;

  FILE *f = fopen(path, "ab");
  if (f) {
    fwrite(content, 1, len, f);
    fclose(f);
  }
  return VM_VOID();
}

VMValue aot_append_file_ptr(VMValue *path_ptr, VMValue *content_ptr) {
  if (!path_ptr || !content_ptr)
    return VM_VOID();
  return aot_append_file(*path_ptr, *content_ptr);
}

VMValue aot_file_exists(VMValue path_val) {
  if (!IS_STRING(path_val))
    return VM_BOOL(false);
  const char *path = AS_STRING(path_val)->chars;
  FILE *f = fopen(path, "r");
  if (f) {
    fclose(f);
    return VM_BOOL(true);
  }
  return VM_BOOL(false);
}

VMValue aot_file_exists_ptr(VMValue *path_ptr) {
  if (!path_ptr)
    return VM_BOOL(false);
  return aot_file_exists(*path_ptr);
}

// sha256(s: str) -> str — lowercase 64-char hex digest of the input
// bytes. Wraps the same `tulpar::sha256_hex` helper the package
// manager and update-cmd already use; the user-facing builtin lets
// .tpr code reach the digest directly (token fingerprints, content
// integrity checks, signed-cookie HMAC building blocks).
//
// Empty string → digest of zero bytes (`e3b0c44...`). Non-string
// argument → empty result; the typeinfer catalog rejects this at
// compile time anyway, so the runtime guard is just defense.
VMValue aot_sha256(VMValue v) {
  if (!IS_STRING(v))
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  ObjString *s = AS_STRING(v);
  std::string hex = tulpar::sha256_hex(s->chars, (size_t)s->length);
  return VM_OBJ((Obj *)aot_allocate_string(hex.data(), (int)hex.size()));
}

VMValue aot_sha256_ptr(VMValue *vp) {
  if (!vp)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  return aot_sha256(*vp);
}

// ----- Password KDF: PBKDF2-HMAC-SHA256 -------------------------------------
// Real password hashing (not bare sha256). Self-describing string format:
//   pbkdf2_sha256$<iters>$<salt_hex>$<dk_hex>
// so verify() reads the salt + iteration count back out of the stored value.
// Built on the in-tree SHA-256 (no OpenSSL dependency) so it always works.

static const int kPwIters = 100000; // ~tens of ms; bump over time
static const int kPwSaltLen = 16;
static const int kPwDkLen = 32; // one HMAC-SHA256 block

static void hmac_sha256(const uint8_t *key, size_t keylen, const uint8_t *msg,
                        size_t msglen, uint8_t out[32]) {
  uint8_t k[64];
  memset(k, 0, sizeof(k));
  if (keylen > 64) {
    tulpar::sha256_raw(key, keylen, k); // key = H(key), rest stays 0-padded
  } else {
    memcpy(k, key, keylen);
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  // inner = H(ipad || msg)
  std::string inner_in;
  inner_in.reserve(64 + msglen);
  inner_in.append((const char *)ipad, 64);
  inner_in.append((const char *)msg, msglen);
  uint8_t inner[32];
  tulpar::sha256_raw(inner_in.data(), inner_in.size(), inner);
  // out = H(opad || inner)
  uint8_t outer_in[64 + 32];
  memcpy(outer_in, opad, 64);
  memcpy(outer_in + 64, inner, 32);
  tulpar::sha256_raw(outer_in, 96, out);
}

// PBKDF2 with dkLen == 32 (single block, so T = U1 ^ U2 ^ ... ^ Uc).
static void pbkdf2_sha256(const uint8_t *pw, size_t pwlen, const uint8_t *salt,
                          size_t saltlen, int iters, uint8_t out[32]) {
  std::string blk;
  blk.reserve(saltlen + 4);
  blk.append((const char *)salt, saltlen);
  const uint8_t idx[4] = {0, 0, 0, 1}; // INT_32_BE(1)
  blk.append((const char *)idx, 4);
  uint8_t u[32];
  hmac_sha256(pw, pwlen, (const uint8_t *)blk.data(), blk.size(), u);
  uint8_t t[32];
  memcpy(t, u, 32);
  for (int i = 1; i < iters; i++) {
    hmac_sha256(pw, pwlen, u, 32, u);
    for (int j = 0; j < 32; j++)
      t[j] ^= u[j];
  }
  memcpy(out, t, 32);
}

static std::string to_hex(const uint8_t *b, size_t n) {
  static const char *h = "0123456789abcdef";
  std::string s;
  s.resize(n * 2);
  for (size_t i = 0; i < n; i++) {
    s[i * 2] = h[b[i] >> 4];
    s[i * 2 + 1] = h[b[i] & 0xf];
  }
  return s;
}

static bool from_hex(const std::string &s, std::vector<uint8_t> &out) {
  if (s.size() % 2 != 0)
    return false;
  out.resize(s.size() / 2);
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < out.size(); i++) {
    int hi = nib(s[i * 2]), lo = nib(s[i * 2 + 1]);
    if (hi < 0 || lo < 0)
      return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

// password_hash(password: str) -> str  (pbkdf2_sha256$iters$salt$dk)
VMValue aot_password_hash(VMValue pwVal) {
  if (!IS_STRING(pwVal))
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  ObjString *pw = AS_STRING(pwVal);

  uint8_t salt[kPwSaltLen];
  std::random_device rd;
  for (int i = 0; i < kPwSaltLen; i++)
    salt[i] = (uint8_t)(rd() & 0xff);

  uint8_t dk[kPwDkLen];
  pbkdf2_sha256((const uint8_t *)pw->chars, (size_t)pw->length, salt, kPwSaltLen,
                kPwIters, dk);

  std::string out = "pbkdf2_sha256$" + std::to_string(kPwIters) + "$" +
                    to_hex(salt, kPwSaltLen) + "$" + to_hex(dk, kPwDkLen);
  return VM_OBJ((Obj *)aot_allocate_string(out.data(), (int)out.size()));
}

VMValue aot_password_hash_ptr(VMValue *vp) {
  if (!vp)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  return aot_password_hash(*vp);
}

// password_verify(password: str, stored: str) -> bool. Constant-time compare.
VMValue aot_password_verify(VMValue pwVal, VMValue storedVal) {
  if (!IS_STRING(pwVal) || !IS_STRING(storedVal))
    return VM_BOOL(0);
  ObjString *pw = AS_STRING(pwVal);
  ObjString *stored = AS_STRING(storedVal);
  std::string s(stored->chars, (size_t)stored->length);

  // Parse pbkdf2_sha256$iters$salt_hex$dk_hex
  const std::string prefix = "pbkdf2_sha256$";
  if (s.size() < prefix.size() || s.compare(0, prefix.size(), prefix) != 0)
    return VM_BOOL(0);
  size_t p1 = s.find('$', prefix.size());
  if (p1 == std::string::npos)
    return VM_BOOL(0);
  size_t p2 = s.find('$', p1 + 1);
  if (p2 == std::string::npos)
    return VM_BOOL(0);
  int iters = atoi(s.substr(prefix.size(), p1 - prefix.size()).c_str());
  if (iters < 1 || iters > 10000000)
    return VM_BOOL(0);
  std::vector<uint8_t> salt, dk;
  if (!from_hex(s.substr(p1 + 1, p2 - p1 - 1), salt))
    return VM_BOOL(0);
  if (!from_hex(s.substr(p2 + 1), dk))
    return VM_BOOL(0);
  if (dk.size() != kPwDkLen || salt.empty())
    return VM_BOOL(0);

  uint8_t calc[kPwDkLen];
  pbkdf2_sha256((const uint8_t *)pw->chars, (size_t)pw->length, salt.data(),
                salt.size(), iters, calc);

  uint8_t diff = 0;
  for (int i = 0; i < kPwDkLen; i++)
    diff |= (uint8_t)(calc[i] ^ dk[i]);
  return VM_BOOL(diff == 0);
}

VMValue aot_password_verify_ptr(VMValue *pw_ptr, VMValue *stored_ptr) {
  if (!pw_ptr || !stored_ptr)
    return VM_BOOL(0);
  return aot_password_verify(*pw_ptr, *stored_ptr);
}

// secure_token(n: int) -> str
// Cryptographically secure random base62 string of length n. Backed by
// std::random_device (CSPRNG / OS entropy: /dev/urandom etc.) — NOT the
// non-crypto rand()/randint path. Unbiased via rejection sampling (drop bytes
// >= 248 = 4*62 before the % 62). Use for session tokens, salts, etc.
VMValue aot_secure_token(VMValue nVal) {
  int64_t n = IS_INT(nVal) ? AS_INT(nVal)
                           : (IS_FLOAT(nVal) ? (int64_t)AS_FLOAT(nVal) : 32);
  if (n < 1) n = 1;
  if (n > 4096) n = 4096;
  static const char abc[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::random_device rd;
  std::string out;
  out.reserve((size_t)n);
  while ((int64_t)out.size() < n) {
    unsigned int v = rd();
    for (int b = 0; b < 4 && (int64_t)out.size() < n; b++) {
      unsigned char byte = (unsigned char)((v >> (b * 8)) & 0xff);
      if (byte < 248) // 4*62, discard the biased tail
        out += abc[byte % 62];
    }
  }
  return VM_OBJ((Obj *)aot_allocate_string(out.data(), (int)out.size()));
}

VMValue aot_secure_token_ptr(VMValue *n_ptr) {
  VMValue n = n_ptr ? *n_ptr : VM_INT(32);
  return aot_secure_token(n);
}

// ============================================================================
// Exception Handling Runtime (setjmp/longjmp based)
// ============================================================================
#include <setjmp.h>

#define EH_STACK_MAX 64

// Exception-handler context: one try-frame stack + the in-flight exception.
// Historically these were file-scope globals. They are now grouped so each
// async coroutine can carry its OWN handler stack: the async runtime swaps the
// active context on every resume/yield (aot_eh_context_swap) so a throw inside
// a suspended coroutine never longjmps into a sibling's stack frame, and an
// uncaught throw rejects that coroutine's promise instead of escaping. Plain
// (non-async) programs only ever use the default `eh_main` context, so their
// behaviour is unchanged.
//
// MUST be thread_local: under a multi-threaded listener (listen_pool /
// listen_async) each worker runs handlers that may use try/reject. With a
// single shared `eh_main`, two workers would push setjmp frames onto the same
// stack and one worker's aot_throw could longjmp into ANOTHER worker's stack
// frame — undefined behaviour / crash. Per-thread storage gives each worker its
// own try-frame stack. The async event loop runs coroutines on one thread, so
// aot_eh_context_swap still operates on that thread's eh_cur as before.
typedef struct EhContext {
  jmp_buf stack[EH_STACK_MAX];
  int depth;
  VMValue exception;
} EhContext;

static thread_local EhContext eh_main = {};
static thread_local EhContext *eh_cur = &eh_main; // active handler context

// Returns pointer to jmp_buf for direct setjmp call
jmp_buf *aot_try_push(void) {
  if (eh_cur->depth >= EH_STACK_MAX) {
    fprintf(stderr, "Exception handler stack overflow\n");
    return nullptr;
  }
  return &eh_cur->stack[eh_cur->depth++];
}

void aot_try_pop(void) {
  if (eh_cur->depth > 0)
    eh_cur->depth--;
}

void aot_throw(VMValue exception) {
  if (eh_cur->depth == 0) {
    fprintf(stderr, "Uncaught Exception: ");
    if (IS_STRING(exception)) {
      fprintf(stderr, "%s\n", AS_STRING(exception)->chars);
    } else if (IS_OBJECT(exception)) {
      fprintf(stderr, "<object type=%d>\n", AS_OBJ(exception)->type);
    } else {
      fprintf(stderr, "<value type=%d>\n", exception.type);
    }
    exit(1);
  }
  eh_cur->exception = exception;
  int target_depth = eh_cur->depth - 1;
  eh_cur->depth = target_depth;
  longjmp(eh_cur->stack[target_depth], 1);
}

// ---- Per-coroutine EH context management (used by runtime/tulpar_async.cpp) -
// A fresh context starts with an empty handler stack. swap() installs `ctx` as
// the active context and returns the previous one, so the async scheduler can
// bracket each coroutine's execution slice and restore the caller's context.
void *aot_eh_context_new(void) {
  EhContext *c = new EhContext();
  c->depth = 0;
  return c;
}
void aot_eh_context_free(void *ctx) { delete static_cast<EhContext *>(ctx); }
void *aot_eh_context_swap(void *ctx) {
  void *prev = eh_cur;
  eh_cur = ctx ? static_cast<EhContext *>(ctx) : &eh_main;
  return prev;
}

void aot_throw_ptr(VMValue *exception_ptr) {
  if (!exception_ptr)
    return;
  aot_throw(*exception_ptr);
}

VMValue aot_get_exception(void) { return eh_cur->exception; }

// ============================================
// Time Functions
// ============================================
#include <ctime>

VMValue aot_clock_ms(void) {
  return VM_FLOAT(tulpar_clock_ms());
}

// ============================================================================
// Socket Functions (AOT)
// ============================================================================
// platform_sockets.h already included at top - provides cross-platform socket API
#define tulpar_socket socket_t
#define tulpar_close tulpar_socket_close
#define tulpar_send send
#define tulpar_recv recv
#define tulpar_invalid_socket INVALID_SOCKET_VALUE

VMValue aot_socket_server(VMValue hostVal, VMValue portVal) {
  if (!IS_STRING(hostVal) || !IS_INT(portVal))
    return VM_INT(-1);

  int port = (int)AS_INT(portVal);

  tulpar_socket server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == tulpar_invalid_socket)
    return VM_INT(-1);

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt,
             sizeof(opt));

  // NOTE: SO_REUSEPORT is deliberately NOT set. With it, two unrelated
  // processes could both bind the same port (kernel load-balances), so a
  // second `serve()` on a busy default port would silently *share* it instead
  // of failing — which defeats Wings' "default port busy → try port+1" and
  // "explicit port busy → tell the user" behaviour. No current path needs it:
  // `listen_pool` binds ONE socket and has its worker threads accept() that
  // shared fd, rather than re-binding per worker. SO_REUSEADDR (above) still
  // covers fast restart through TIME_WAIT.

  // Resolve the `host` argument. Previously this was *ignored* and
  // we always bound to `INADDR_ANY`, which is wrong in two ways:
  //
  //   1. A wings user calling `socket_server("127.0.0.1", port)` got
  //      a socket bound to every interface anyway. A dev-only call
  //      that should have stayed strictly local was reachable from
  //      the LAN — a small but real security smell.
  //
  //   2. On Windows, the first time a binary binds a non-loopback
  //      listener Windows Firewall pops "Allow this app to
  //      communicate?" and waits for a user click. Every test run
  //      that builds a fresh binary in a tempdir triggers a fresh
  //      prompt (firewall rules are keyed on the absolute exe path),
  //      which made iterating on benchmarks and smoke tests very
  //      painful. Loopback bindings are exempt from the prompt, so
  //      honouring "127.0.0.1" properly fixes the firewall pain
  //      without any allowlisting or signing.
  //
  // The parse is permissive: unknown / unparseable host strings
  // fall back to ANY rather than failing, so any existing user code
  // passing an odd value keeps the wildcard behaviour it used to get.
  struct sockaddr_in address;
  address.sin_family = AF_INET;
  const char *host_str = AS_STRING(hostVal)->chars;
  if (host_str && (strcmp(host_str, "127.0.0.1") == 0 ||
                   strcmp(host_str, "localhost") == 0)) {
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else if (!host_str || host_str[0] == '\0' ||
             strcmp(host_str, "0.0.0.0") == 0 ||
             strcmp(host_str, "::") == 0) {
    address.sin_addr.s_addr = INADDR_ANY;
  } else {
    struct in_addr parsed;
#if PLATFORM_WINDOWS
    if (InetPtonA(AF_INET, host_str, &parsed) == 1) {
#else
    if (inet_pton(AF_INET, host_str, &parsed) == 1) {
#endif
      address.sin_addr = parsed;
    } else {
      address.sin_addr.s_addr = INADDR_ANY;
    }
  }
  address.sin_port = htons((uint16_t)port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    tulpar_close(server_fd);
    return VM_INT(-1);
  }

  // Backlog was 3. On any modern Linux with 4+ concurrent connections
  // (e.g. the benchmark harness's 4-worker parallel connect), the
  // accept queue overflows before the server's main thread can drain
  // it; the kernel silently drops the SYN-ACK on the overflow path
  // (default `tcp_abort_on_overflow=0`), the client's `connect()`
  // hangs or fails, and the bench's barrier-on-failure logic aborts
  // every worker — manifesting as 0 rps in the CI HTTP results table
  // for `listen_async` and `listen_pool` (caught the long way on
  // GitHub Actions). SOMAXCONN is the largest backlog the kernel
  // accepts; on Linux 4096+ on modern distros, on Windows it's
  // INT_MAX. Either way it's "as big as the kernel will allow," which
  // is what we want for a long-running server.
  if (listen(server_fd, SOMAXCONN) < 0) {
    tulpar_close(server_fd);
    return VM_INT(-1);
  }
  return VM_INT(server_fd);
}

// ---------------------------------------------------------------------------
// http_request(method, url, body) -> json (or VM_INT(0) on error)
//
// Outbound HTTP/1.0 client. Plain HTTP always; HTTPS supported when
// Tulpar is built with OpenSSL (`-DTULPAR_HAS_TLS=1`). Currently:
// no redirects, no chunked encoding, no keep-alive on the client
// side. Enough to hit local services, internal admin APIs, public
// JSON endpoints over HTTPS.
//
// Returns an object:
//   { "ok": 1, "status": <int>, "headers": <obj>, "body": <str> }
// or  { "ok": 0, "error": "<reason>" } on failure.
//
// Uses getaddrinfo so hostnames work (the existing socket_client uses
// inet_pton and only accepts IPs).
// ---------------------------------------------------------------------------

#if defined(_WIN32)
  // ws2tcpip.h is already pulled in by platform_sockets.h on Windows.
#else
  #include <netdb.h>
#endif

// Forward declarations — these helpers are defined further down in the
// file (next to http_parse_request); aot_http_request is the first
// caller above their definition site.
static ObjObject *aot_http_make_obj(int initial_capacity);
static void aot_http_obj_set(ObjObject *o, const char *key, int key_len,
                             VMValue val);
static inline void aot_http_obj_set_str(ObjObject *o, const char *key, int klen,
                                        const char *val, int vlen);

// Bridge into the namespaced http_fetch helpers. The `#include` lives
// outside any `extern "C"` block to keep C++ linkage on the namespace
// declarations; that's why we declare the prototype manually here.
} // close extern "C" before including the C++-namespaced header
#include "../common/http_fetch.hpp"
extern "C" {

namespace {

// Parse `http://host[:port]/path?query` into pieces. Stores results in
// `out_*`. Returns false on malformed input.
bool parse_http_url(const char *url, std::string &out_host, int &out_port,
                    std::string &out_path) {
    if (!url) return false;
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        // No TLS support. Fail loud.
        return false;
    } else {
        return false;  // require explicit scheme
    }

    const char *host_end = p;
    while (*host_end && *host_end != ':' && *host_end != '/') host_end++;
    out_host.assign(p, host_end - p);
    if (out_host.empty()) return false;

    out_port = 80;
    if (*host_end == ':') {
        const char *port_start = host_end + 1;
        const char *port_end = port_start;
        while (*port_end >= '0' && *port_end <= '9') port_end++;
        if (port_end == port_start) return false;
        out_port = atoi(port_start);
        host_end = port_end;
    }

    if (*host_end == '/') {
        out_path = host_end;
    } else if (*host_end == '\0') {
        out_path = "/";
    } else {
        return false;
    }
    return true;
}

tulpar_socket http_dial(const char *host, int port) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        return tulpar_invalid_socket;
    }
    tulpar_socket sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == tulpar_invalid_socket) {
        freeaddrinfo(res);
        return tulpar_invalid_socket;
    }
    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) < 0) {
        tulpar_close(sock);
        freeaddrinfo(res);
        return tulpar_invalid_socket;
    }
    freeaddrinfo(res);
    return sock;
}

}  // namespace

// Build a { ok:0, error } envelope. Shared by the blocking and async paths.
static VMValue aot_http_error_obj(const char *msg) {
    ObjObject *r = aot_http_make_obj(2);
    aot_http_obj_set(r, "ok", 2, VM_INT(0));
    aot_http_obj_set_str(r, "error", 5, msg, (int)strlen(msg));
    return VM_OBJ((Obj *)r);
}

// Parse a raw HTTP response buffer (status line + headers + body) into the
// { ok:1, status, headers, body } object. Shared by aot_http_request (sync)
// and the async completion path. MUST run on the main thread — it allocates
// VM objects/strings, which are not safe to touch from a worker thread.
static VMValue aot_http_build_response(const std::string &buf) {
    // Parse status line
    size_t line_end = buf.find('\n');
    if (line_end == std::string::npos) return aot_http_error_obj("malformed response");
    std::string status_line = buf.substr(0, line_end);
    if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();

    int status = 0;
    {
        size_t sp = status_line.find(' ');
        if (sp == std::string::npos) return aot_http_error_obj("malformed status line");
        size_t sp2 = status_line.find(' ', sp + 1);
        std::string code = status_line.substr(sp + 1,
            (sp2 == std::string::npos ? status_line.size() : sp2) - sp - 1);
        status = atoi(code.c_str());
    }

    // Parse headers
    ObjObject *headers = aot_http_make_obj(8);
    size_t pos = line_end + 1;
    while (pos < buf.size()) {
        size_t le = buf.find('\n', pos);
        if (le == std::string::npos) break;
        std::string h = buf.substr(pos, le - pos);
        if (!h.empty() && h.back() == '\r') h.pop_back();
        pos = le + 1;
        if (h.empty()) break;
        size_t colon = h.find(':');
        if (colon == std::string::npos) continue;
        std::string k = h.substr(0, colon);
        size_t v_start = colon + 1;
        while (v_start < h.size() && (h[v_start] == ' ' || h[v_start] == '\t'))
            v_start++;
        std::string v = h.substr(v_start);
        aot_http_obj_set_str(headers, k.data(), (int)k.size(),
                             v.data(), (int)v.size());
    }

    std::string body_str = pos < buf.size() ? buf.substr(pos) : std::string();

    ObjObject *out = aot_http_make_obj(4);
    aot_http_obj_set(out, "ok", 2, VM_INT(1));
    aot_http_obj_set(out, "status", 6, VM_INT(status));
    aot_http_obj_set(out, "headers", 7, VM_OBJ((Obj *)headers));
    aot_http_obj_set_str(out, "body", 4, body_str.data(), (int)body_str.size());
    return VM_OBJ((Obj *)out);
}

VMValue aot_http_request(VMValue methodVal, VMValue urlVal, VMValue bodyVal) {
    if (!IS_STRING(methodVal) || !IS_STRING(urlVal))
        return aot_http_error_obj("bad args");
    std::string method = AS_STRING(methodVal)->chars;
    std::string url = AS_STRING(urlVal)->chars;
    std::string body;
    if (IS_STRING(bodyVal)) {
        body.assign(AS_STRING(bodyVal)->chars, AS_STRING(bodyVal)->length);
    }

    // Delegate to the shared http_fetch implementation so HTTPS
    // (when TLS is compiled in) is supported uniformly with the
    // package manager's registry fetch.
    std::string buf, fetch_err;
    if (!tulpar::http_request_url(method, url, body, buf, fetch_err)) {
        return aot_http_error_obj(fetch_err.c_str());
    }
    return aot_http_build_response(buf);
}

// ---------------------------------------------------------------------------
// Async HTTP — http_request_async(method, url, body) -> promise<json>
//
// Offloads the blocking request to a tiny worker pool so the single-threaded
// async event loop keeps pumping other coroutines while the socket I/O is in
// flight. Worker threads only do the network leg (http_request_url, filling
// std::strings); the main thread parses the buffer into VM objects and settles
// the promise in http_async_poll(). Nothing on the worker side touches the
// scheduler, so the handoff needs only one atomic flag per job — no locking of
// the ready queue / timers. Pool size defaults to 4, override TULPAR_HTTP_POOL.
// ---------------------------------------------------------------------------
extern "C" {
ObjPromise *aot_promise_new(void);
void aot_promise_settle(ObjPromise *p, VMValue value, int state);
void aot_io_register(int (*poll)(void *ud), void *ud);
}

namespace {

struct HttpAsyncJob {
    std::string method, url, body; // input (owned copies, read by worker)
    std::string buf, err;          // output (written by worker)
    bool ok = false;
    std::atomic<int> done{0};      // 0 pending, 1 finished (release/acquire)
    ObjPromise *promise = nullptr;
};

tulpar_mutex_t g_http_pool_mtx;
std::vector<HttpAsyncJob *> g_http_pool_queue; // guarded by g_http_pool_mtx
bool g_http_pool_inited = false;

#if PLATFORM_WINDOWS
unsigned __stdcall http_pool_worker(void *) {
#else
void *http_pool_worker(void *) {
#endif
    for (;;) {
        HttpAsyncJob *job = nullptr;
        tulpar_mutex_lock(&g_http_pool_mtx);
        if (!g_http_pool_queue.empty()) {
            job = g_http_pool_queue.front();
            g_http_pool_queue.erase(g_http_pool_queue.begin());
        }
        tulpar_mutex_unlock(&g_http_pool_mtx);
        if (!job) {
            tulpar_thread_sleep(1); // idle: nothing queued
            continue;
        }
        job->ok = tulpar::http_request_url(job->method, job->url, job->body,
                                           job->buf, job->err);
        job->done.store(1, std::memory_order_release);
    }
#if PLATFORM_WINDOWS
    return 0;
#else
    return nullptr;
#endif
}

// Event-loop completion poll (main thread). Returns 1 once the worker has
// finished — having first built the response object and settled the promise —
// so the loop drops this source.
int http_async_poll(void *ud) {
    HttpAsyncJob *job = (HttpAsyncJob *)ud;
    if (job->done.load(std::memory_order_acquire) == 0) return 0;
    VMValue result = job->ok ? aot_http_build_response(job->buf)
                             : aot_http_error_obj(job->err.c_str());
    aot_promise_settle(job->promise, result, 1);
    delete job;
    return 1;
}

void http_pool_init() {
    if (g_http_pool_inited) return;
    g_http_pool_inited = true;
    tulpar_mutex_init(&g_http_pool_mtx);
    int n = 4;
    const char *env = getenv("TULPAR_HTTP_POOL");
    if (env && *env) {
        n = atoi(env);
        if (n < 1) n = 1;
        if (n > 64) n = 64;
    }
    for (int i = 0; i < n; i++) {
        tulpar_thread_t th;
        if (tulpar_thread_create(&th, (tulpar_thread_func_t)http_pool_worker,
                                 nullptr) == 0)
            tulpar_thread_detach(th);
    }
}

}  // namespace

VMValue aot_http_request_async(VMValue methodVal, VMValue urlVal,
                               VMValue bodyVal) {
    ObjPromise *p = aot_promise_new();
    if (!IS_STRING(methodVal) || !IS_STRING(urlVal)) {
        aot_promise_settle(p, aot_http_error_obj("bad args"), 1);
        return VM_OBJ((Obj *)p);
    }
    http_pool_init();

    HttpAsyncJob *job = new HttpAsyncJob();
    job->promise = p;
    job->method = AS_STRING(methodVal)->chars;
    job->url = AS_STRING(urlVal)->chars;
    if (IS_STRING(bodyVal))
        job->body.assign(AS_STRING(bodyVal)->chars, AS_STRING(bodyVal)->length);

    tulpar_mutex_lock(&g_http_pool_mtx);
    g_http_pool_queue.push_back(job);
    tulpar_mutex_unlock(&g_http_pool_mtx);

    aot_io_register(&http_async_poll, job);
    return VM_OBJ((Obj *)p);
}

VMValue aot_socket_client(VMValue hostVal, VMValue portVal) {
  if (!IS_STRING(hostVal) || !IS_INT(portVal))
    return VM_INT(-1);

  const char *host = AS_STRING(hostVal)->chars;
  int port = (int)AS_INT(portVal);

  tulpar_socket sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == tulpar_invalid_socket)
    return VM_INT(-1);

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons((uint16_t)port);

  if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
    tulpar_close(sock);
    return VM_INT(-1);
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    tulpar_close(sock);
    return VM_INT(-1);
  }
  return VM_INT(sock);
}

VMValue aot_socket_accept(VMValue serverFdVal) {
  if (!IS_INT(serverFdVal))
    return VM_INT(-1);
  tulpar_socket server_fd = (tulpar_socket)AS_INT(serverFdVal);

  struct sockaddr_in address;
  socklen_t addrlen = sizeof(address);
  tulpar_socket new_socket =
      accept(server_fd, (struct sockaddr *)&address, &addrlen);

  // TCP_NODELAY on the accepted socket. Without it, Nagle's algorithm
  // batches small writes (typical for JSON API responses < 1KB) for up
  // to 40ms before flushing — a measurable hit on keep-alive throughput
  // and a latency cliff on request/response benchmarks. Cost: zero;
  // we only ever do one send() per response and don't write multi-byte
  // streams that would benefit from coalescing.
#if PLATFORM_WINDOWS
  if ((int64_t)new_socket != -1) {
    int one = 1;
    setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY,
               (const char *)&one, sizeof(one));
  }
#else
  if ((int64_t)new_socket >= 0) {
    int one = 1;
    setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  }
#endif

  return VM_INT((int64_t)new_socket);
}

// socket_peer_ip(fd) -> str. Resolves the remote (client) address of an
// accepted connection via getpeername()+inet_ntop. Used by the wings /
// router dispatchers to populate `_request["remote_addr"]` so middleware
// (rate limiter, audit logging) can key on the real client instead of a
// hardcoded "127.0.0.1". Returns "" on any error (bad fd, unconnected
// socket, non-IPv4 family) so callers can fall back gracefully.
VMValue aot_socket_peer_ip(VMValue fdVal) {
  if (!IS_INT(fdVal))
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  tulpar_socket fd = (tulpar_socket)AS_INT(fdVal);

  struct sockaddr_in addr;
  socklen_t alen = sizeof(addr);
  if (getpeername(fd, (struct sockaddr *)&addr, &alen) != 0)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  char buf[INET_ADDRSTRLEN];
  const char *ip = inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
  if (!ip)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  return VM_OBJ((Obj *)aot_allocate_string(ip, (int)strlen(ip)));
}

VMValue aot_socket_send(VMValue fdVal, VMValue dataVal) {
  if (!IS_INT(fdVal) || !IS_STRING(dataVal))
    return VM_INT(-1);
  tulpar_socket fd = (tulpar_socket)AS_INT(fdVal);
  ObjString *s = AS_STRING(dataVal);

  ssize_t sent = (ssize_t)tulpar_send(fd, s->chars, s->length, 0);
  return VM_INT((int64_t)sent);
}

VMValue aot_socket_receive(VMValue fdVal, VMValue sizeVal) {
  if (!IS_INT(fdVal) || !IS_INT(sizeVal))
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  tulpar_socket fd = (tulpar_socket)AS_INT(fdVal);
  int size = (int)AS_INT(sizeVal);

  char *buffer = static_cast<char*>(malloc(size + 1));
  if (!buffer)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  ssize_t valread = (ssize_t)tulpar_recv(fd, buffer, size, 0);
  if (valread < 0)
    valread = 0;
  buffer[valread] = 0;

  ObjString *res = aot_allocate_string(buffer, valread);
  free(buffer);
  return VM_OBJ((Obj *)res);
}

// Returns VMValue rather than void: the AOT codegen calls this through
// an LLVM function pointer typed `(VMValue) -> VMValue`. On Windows
// MinGW64 the void-return / struct-arg variant trips a calling-convention
// mismatch (the SysV-shaped VMValue payload sits in registers the void
// caller expects to be scratch), which manifests as a crash on first
// invocation. Returning VMValue keeps the ABI uniform with every other
// builtin and incurs zero cost — `close()` still happens, we just hand
// back a sentinel zero.
VMValue aot_socket_close(VMValue fdVal) {
  if (IS_INT(fdVal)) {
    tulpar_close((tulpar_socket)AS_INT(fdVal));
  }
  return VM_INT(0);
}

// Set a socket to non-blocking mode. Returns 1 on success, 0 on failure.
// Used by `listen_evented` so accept() and recv() don't park the entire
// event loop when the client hasn't filled its TCP buffers yet.
VMValue aot_socket_set_nonblocking(VMValue fdVal) {
  if (!IS_INT(fdVal))
    return VM_INT(0);
  tulpar_socket fd = (tulpar_socket)AS_INT(fdVal);
  return VM_INT(tulpar_socket_set_nonblocking(fd, 1) == 0 ? 1 : 0);
}

// Poll an array of fds for read-ready events. Input is a Tulpar `json`
// array of integer fds; output is a fresh `json` array of indices into
// the input that have POLLIN | POLLERR | POLLHUP set after the call.
//
// `timeout_ms` follows poll(2) semantics: 0 = non-blocking probe,
// negative = block forever, positive = block up to N ms.
//
// We watch POLLIN only (not POLLOUT) because the simple per-request
// model in `listen_evented` does send() blocking after handler dispatch
// — the trade-off is no partial-write resume, which would matter for
// streaming responses but doesn't for our typical < 64 KiB JSON
// payloads. POLLERR / POLLHUP go straight to "fire" so the loop can
// observe peer disconnect and clean up the slot.
VMValue aot_socket_poll(VMValue fdsVal, VMValue timeoutVal) {
  // Allocator helper: build an ObjArray with the given count of items
  // copied from `src` (or zeros if src is null). Arena-allocated so the
  // per-request reset reclaims memory automatically inside Wings.
  auto make_arr = [](int n, const VMValue *src) -> VMValue {
    ObjArray *a = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
    a->obj.type = OBJ_ARRAY;
    a->obj.arena_allocated = 1;
    a->obj.next = nullptr;
    a->obj.ref_count = 1;
    a->obj.is_moved = 0;
    a->capacity = n;
    a->count = n;
    if (n > 0) {
      a->items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * (size_t)n);
      if (src) {
        for (int i = 0; i < n; i++) a->items[i] = src[i];
      }
    } else {
      a->items = nullptr;
    }
    return VM_OBJ((Obj *)a);
  };

  if (!IS_ARRAY(fdsVal)) {
    return make_arr(0, nullptr);
  }
  ObjArray *arr = AS_ARRAY(fdsVal);
  int nfds = (int)arr->count;
  if (nfds <= 0) {
    return make_arr(0, nullptr);
  }
  // Cap to a sane maximum; 4096 fds in one Tulpar process is well past
  // what `listen_evented` will ever reach and keeps stack usage bounded.
  if (nfds > 4096) nfds = 4096;

  // Stack buffer for small fd counts (the common case — a server with
  // < 256 concurrent conns) so we don't malloc/free per tick.
  tulpar_pollfd small_buf[256];
  tulpar_pollfd *pfds = (nfds <= 256)
                            ? small_buf
                            : (tulpar_pollfd *)malloc(sizeof(tulpar_pollfd) * (size_t)nfds);
  if (!pfds) {
    return make_arr(0, nullptr);
  }

  for (int i = 0; i < nfds; i++) {
    VMValue v = arr->items[i];
    int64_t fd = IS_INT(v) ? AS_INT(v) : -1;
    pfds[i].fd = (tulpar_socket)fd;
    pfds[i].events = POLLIN;
    pfds[i].revents = 0;
  }

  int timeout_ms = IS_INT(timeoutVal) ? (int)AS_INT(timeoutVal) : 0;
  int rc = tulpar_socket_poll(pfds, (unsigned int)nfds, timeout_ms);

  // Walk results into a stack buffer first; sized to nfds so it always
  // fits even with everything firing at once.
  VMValue stack_ready[256];
  VMValue *ready_buf = (nfds <= 256)
                           ? stack_ready
                           : (VMValue *)malloc(sizeof(VMValue) * (size_t)nfds);
  int ready_count = 0;
  if (rc > 0 && ready_buf) {
    for (int i = 0; i < nfds; i++) {
      if (pfds[i].revents & (POLLIN | POLLERR | POLLHUP)) {
        ready_buf[ready_count++] = VM_INT((int64_t)i);
      }
    }
  }

  VMValue out = make_arr(ready_count, ready_buf);
  if (pfds != small_buf) free(pfds);
  if (ready_buf != stack_ready) free(ready_buf);
  return out;
}

// ============================================================================
// TLS server primitives — power Wings TLS listener (lib/wings_tls.tpr)
// ============================================================================
//
// Mirrors the client-side OpenSSL path in src/common/http_fetch.cpp but
// flips the direction: SSL_CTX uses TLS_server_method() + a cert/key
// pair the user supplies, every accepted fd gets wrapped in SSL_accept,
// and read/write go through SSL_read / SSL_write instead of recv / send.
//
// The Tulpar surface keeps the SSL pointer as an opaque int64 (raw
// reinterpret_cast of `SSL *`). User code holds it as `int ssl` and
// passes it to the four helpers; lifetime is tied to `tls_close`. We
// keep the SSL_CTX alive for the listener's whole lifetime — one
// per call to `tls_listen` — so cert/key files are read once at
// startup and shared across every connection that listener serves.
//
// Build: gated on TULPAR_HAS_TLS (set by CMake when find_package(OpenSSL)
// succeeds). Without TLS the builtins return error sentinels so calling
// code degrades gracefully — Tulpar binaries built on a host without
// OpenSSL still run, just without the TLS surface.

#if defined(TULPAR_HAS_TLS)
  #include <openssl/ssl.h>
  #include <openssl/err.h>
#endif

// Initialize a TLS server context. Loads the cert + key from disk,
// configures TLS_server_method, and returns the SSL_CTX pointer as
// int64 for Tulpar code to thread back into tls_accept. Returns 0 on
// any failure (invalid paths, mismatched cert/key, or no-TLS build).
VMValue aot_tls_init(VMValue certVal, VMValue keyVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_STRING(certVal) || !IS_STRING(keyVal)) return VM_INT(0);
  const char *cert_path = AS_STRING(certVal)->chars;
  const char *key_path = AS_STRING(keyVal)->chars;
  // OpenSSL 1.1.0+ initializes itself lazily; older versions need
  // OPENSSL_init_ssl(0, nullptr) but we already require modern OpenSSL
  // for the client-side TLS support.
  SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
  if (!ctx) return VM_INT(0);
  // Reasonable defaults: TLS 1.2+, no SSLv3 / TLS 1.0/1.1.
  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
  if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) != 1) {
    fprintf(stderr, "[tls] use_certificate_file failed for %s\n", cert_path);
    SSL_CTX_free(ctx);
    return VM_INT(0);
  }
  if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) != 1) {
    fprintf(stderr, "[tls] use_PrivateKey_file failed for %s\n", key_path);
    SSL_CTX_free(ctx);
    return VM_INT(0);
  }
  if (SSL_CTX_check_private_key(ctx) != 1) {
    fprintf(stderr, "[tls] check_private_key failed (cert/key mismatch?)\n");
    SSL_CTX_free(ctx);
    return VM_INT(0);
  }
  return VM_INT((int64_t)(uintptr_t)ctx);
#else
  (void)certVal; (void)keyVal;
  return VM_INT(0);
#endif
}

// Accept + SSL_accept on a connection that arrived on a plain TCP
// listening socket. The caller keeps using `socket_accept(server)` to
// get the raw fd, then hands the fd here along with the ctx returned
// from tls_init. We do the SSL_accept handshake synchronously and hand
// back the SSL* (cast to int64) on success, 0 on failure. On failure
// we close the underlying socket so the caller doesn't have to track
// it separately.
VMValue aot_tls_accept(VMValue ctxVal, VMValue clientFdVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(ctxVal) || !IS_INT(clientFdVal)) return VM_INT(0);
  SSL_CTX *ctx = (SSL_CTX *)(uintptr_t)AS_INT(ctxVal);
  tulpar_socket fd = (tulpar_socket)AS_INT(clientFdVal);
  if (!ctx) return VM_INT(0);
  SSL *ssl = SSL_new(ctx);
  if (!ssl) return VM_INT(0);
  if (SSL_set_fd(ssl, (int)fd) != 1) {
    SSL_free(ssl);
    return VM_INT(0);
  }
  int hs = SSL_accept(ssl);
  if (hs != 1) {
    int err = SSL_get_error(ssl, hs);
    fprintf(stderr, "[tls] SSL_accept failed err=%d\n", err);
    SSL_free(ssl);
    tulpar_close(fd);
    return VM_INT(0);
  }
  return VM_INT((int64_t)(uintptr_t)ssl);
#else
  (void)ctxVal; (void)clientFdVal;
  return VM_INT(0);
#endif
}

// Read up to `max_bytes` from a TLS connection. Returns the bytes as
// a Tulpar string. Empty string on EOF or error — same shape as
// `socket_receive` so wings handler logic can drive both sides with
// the same contract.
VMValue aot_tls_recv(VMValue sslVal, VMValue maxVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(sslVal) || !IS_INT(maxVal)) {
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  }
  SSL *ssl = (SSL *)(uintptr_t)AS_INT(sslVal);
  int max_bytes = (int)AS_INT(maxVal);
  if (!ssl || max_bytes <= 0 || max_bytes > 16 * 1024 * 1024) {
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  }
  char *buf = (char *)malloc((size_t)max_bytes + 1);
  if (!buf) return VM_OBJ((Obj *)aot_allocate_string("", 0));
  int n = SSL_read(ssl, buf, max_bytes);
  if (n <= 0) {
    free(buf);
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  }
  buf[n] = 0;
  ObjString *res = aot_allocate_string(buf, n);
  free(buf);
  return VM_OBJ((Obj *)res);
#else
  (void)sslVal; (void)maxVal;
  return VM_OBJ((Obj *)aot_allocate_string("", 0));
#endif
}

// Write a Tulpar string to a TLS connection. Returns the byte count
// SSL_write reported, or -1 on error / closed connection. Single-shot;
// no partial-write retry, mirroring the existing socket_send shape.
// Wings' typical < 64 KiB JSON responses fit inside a single SSL_write.
VMValue aot_tls_send(VMValue sslVal, VMValue dataVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(sslVal) || !IS_STRING(dataVal)) return VM_INT(-1);
  SSL *ssl = (SSL *)(uintptr_t)AS_INT(sslVal);
  ObjString *s = AS_STRING(dataVal);
  if (!ssl || !s) return VM_INT(-1);
  int n = SSL_write(ssl, s->chars, (int)s->length);
  return VM_INT((int64_t)n);
#else
  (void)sslVal; (void)dataVal;
  return VM_INT(-1);
#endif
}

// Tear down an SSL connection and close the underlying TCP fd. Idempotent
// on a NULL pointer — handler code that hits an early SSL_accept failure
// and calls tls_close(0) shouldn't crash.
VMValue aot_tls_close(VMValue sslVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(sslVal)) return VM_INT(0);
  SSL *ssl = (SSL *)(uintptr_t)AS_INT(sslVal);
  if (!ssl) return VM_INT(0);
  // Best-effort graceful shutdown — clients that already closed don't
  // need a response, and SSL_shutdown returning 0 is normal in that case.
  SSL_shutdown(ssl);
  int fd = SSL_get_fd(ssl);
  SSL_free(ssl);
  if (fd >= 0) tulpar_close((tulpar_socket)fd);
  return VM_INT(0);
#else
  (void)sslVal;
  return VM_INT(0);
#endif
}

// Free an SSL_CTX returned by tls_init. Call once per listener at
// shutdown — typically never in practice because Wings servers run
// to SIGTERM, but exposed so embed-style use can clean up explicitly.
VMValue aot_tls_ctx_free(VMValue ctxVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(ctxVal)) return VM_INT(0);
  SSL_CTX *ctx = (SSL_CTX *)(uintptr_t)AS_INT(ctxVal);
  if (ctx) SSL_CTX_free(ctx);
  return VM_INT(0);
#else
  (void)ctxVal;
  return VM_INT(0);
#endif
}

// ============================================================================
// Threading Functions (AOT) - Cross-platform threading
// ============================================================================
#include "../common/platform_threads.h"

// cpu_count() -> int (logical CPUs as seen by the OS)
//
// Exposes `tulpar_get_cpu_count()` (sysconf(_SC_NPROCESSORS_ONLN) on
// POSIX, GetSystemInfo().dwNumberOfProcessors on Windows). Used by
// wings.tpr's `listen_pool` to default its worker count to the
// host's CPU count when the caller didn't explicitly pick one — a
// hardcoded `x8` over-subscribes on a 4-vCPU CI runner and was the
// difference between threading variants beating Node by 1.9× (low
// conc, no over-subscription) and barely 1.2× (high conc, 8 workers
// thrashing on 4 cores).
VMValue aot_cpu_count(void) {
  int n = tulpar_get_cpu_count();
  if (n < 1) n = 1;
  return VM_INT((int64_t)n);
}

// Thread argument structure
typedef struct {
  void *func_ptr; // Function pointer to call
  VMValue arg;    // Argument to pass
} AOTThreadArgs;

// Thread entry point wrapper.
//
// Tulpar user functions are emitted by `codegen_func_def`
// (src/aot/llvm_backend.cpp) with the signature
// `void func(VMValue *result_ptr, VMValue *arg1_ptr, ...)` — every
// argument is a *pointer* to a VMValue, never a VMValue by value.
//
// Two historical bugs lived here:
//   1. The original typedef was `VMValue (*)(VMValue)` (return-by-
//      value). Fixed earlier to `void (*)(VMValue *, VMValue)`.
//   2. The earlier fix STILL passed the argument by value. On Windows
//      x64 MS-ABI, 16-byte VMValue structs are automatically passed
//      by hidden pointer, so the by-value call accidentally matched
//      the codegen'd pointer-taking ABI. On Linux/macOS SysV-ABI,
//      16-byte structs are split into two integer registers — the
//      callee then read those register contents as if they were a
//      pointer, dereferenced garbage, and segfaulted on first arg
//      access. Manifested as `listen_async` / `listen_pool` failing
//      with empty bodies / 0 rps on CI Linux (caught via WSL repro
//      with gdb pointing to `t.wings_pool_worker`'s first instruction).
//
// Fix: declare the typedef with `VMValue *` for the arg and pass
// `&targs->arg`. Now matches the codegen'd ABI on every platform.
// `targs` lives for the duration of this function (free()'d below),
// so taking its address is safe.
#if PLATFORM_WINDOWS
static unsigned __stdcall aot_thread_entry(void *arg) {
#else
static void *aot_thread_entry(void *arg) {
#endif
  AOTThreadArgs *targs = (AOTThreadArgs *)arg;

  typedef void (*ThreadFunc)(VMValue *result, VMValue *arg);
  ThreadFunc func = (ThreadFunc)targs->func_ptr;

  if (func) {
    VMValue result = VM_VOID();
    func(&result, &targs->arg);
  }

  free(targs);
#if PLATFORM_WINDOWS
  return 0;
#else
  return nullptr;
#endif
}

// thread_create(func_ptr, arg) -> thread_id
VMValue aot_thread_create(void *func_ptr, VMValue arg) {
  tulpar_thread_t thread;

  AOTThreadArgs *targs = static_cast<AOTThreadArgs*>(malloc(sizeof(AOTThreadArgs)));
  if (!targs)
    return VM_INT(-1);

  targs->func_ptr = func_ptr;
  targs->arg = arg;

#if PLATFORM_WINDOWS
  int result = tulpar_thread_create(&thread, (tulpar_thread_func_t)aot_thread_entry, targs);
#else
  int result = tulpar_thread_create(&thread, aot_thread_entry, targs);
#endif
  if (result != 0) {
    free(targs);
    return VM_INT(-1);
  }

  // Return thread ID as int64
  return VM_INT((int64_t)(uintptr_t)thread);
}

// thread_join / thread_detach return VMValue (sentinel 0) instead of
// void to dodge the MinGW64 (VMValue) -> void calling-convention bug
// — same fix we applied to `aot_socket_close` earlier. Without this,
// `thread_detach(t)` crashes immediately on first call.
VMValue aot_thread_join(VMValue threadVal) {
  if (!IS_INT(threadVal)) return VM_INT(0);
  tulpar_thread_t thread = (tulpar_thread_t)(uintptr_t)AS_INT(threadVal);
  tulpar_thread_join(thread);
  return VM_INT(0);
}

VMValue aot_thread_detach(VMValue threadVal) {
  if (!IS_INT(threadVal)) return VM_INT(0);
  tulpar_thread_t thread = (tulpar_thread_t)(uintptr_t)AS_INT(threadVal);
  tulpar_thread_detach(thread);
  return VM_INT(0);
}

// ============================================================================
// Mutex Functions (AOT) - Cross-platform mutex
// ============================================================================

// mutex_create() -> mutex_ptr (as int64)
VMValue aot_mutex_create(void) {
  tulpar_mutex_t *mutex = static_cast<tulpar_mutex_t*>(malloc(sizeof(tulpar_mutex_t)));
  if (!mutex)
    return VM_INT(0);

  tulpar_mutex_init(mutex);

  // Return mutex pointer as int64
  return VM_INT((int64_t)(uintptr_t)mutex);
}

// VMValue-returning to dodge the same MinGW64 (VMValue) -> void ABI
// quirk that bit `aot_socket_close` and `aot_thread_join`. Returning
// a sentinel zero is free.
VMValue aot_mutex_lock(VMValue mutexVal) {
  if (!IS_INT(mutexVal)) return VM_INT(0);
  tulpar_mutex_t *mutex = (tulpar_mutex_t *)(uintptr_t)AS_INT(mutexVal);
  if (mutex) tulpar_mutex_lock(mutex);
  return VM_INT(0);
}

VMValue aot_mutex_unlock(VMValue mutexVal) {
  if (!IS_INT(mutexVal)) return VM_INT(0);
  tulpar_mutex_t *mutex = (tulpar_mutex_t *)(uintptr_t)AS_INT(mutexVal);
  if (mutex) tulpar_mutex_unlock(mutex);
  return VM_INT(0);
}

VMValue aot_mutex_destroy(VMValue mutexVal) {
  if (!IS_INT(mutexVal)) return VM_INT(0);
  tulpar_mutex_t *mutex = (tulpar_mutex_t *)(uintptr_t)AS_INT(mutexVal);
  if (mutex) {
    tulpar_mutex_destroy(mutex);
    free(mutex);
  }
  return VM_INT(0);
}

// ============================================================================
// HTTP Parsing Functions (AOT)
// ============================================================================

// Parse HTTP request: returns JSON object with method, path, headers, body
// ----------------------------------------------------------------------------
// Internal helpers for the native HTTP parser.
// ----------------------------------------------------------------------------
//
// Builds a JSON-style object in the AOT arena. Keeps allocation logic
// localised so future parse fields (cookies, multipart, ...) only touch one
// place. Key strings are copied by aot_allocate_string.
static ObjObject *aot_http_make_obj(int initial_capacity) {
  ObjObject *o = (ObjObject *)aot_arena_alloc(sizeof(ObjObject));
  o->obj.type = OBJ_OBJECT;
  o->obj.arena_allocated = 1;
  o->obj.next = nullptr;
  o->capacity = initial_capacity > 0 ? initial_capacity : 4;
  o->count = 0;
  o->keys = (ObjString **)aot_arena_alloc(sizeof(ObjString *) * o->capacity);
  o->values = (VMValue *)aot_arena_alloc(sizeof(VMValue) * o->capacity);
  return o;
}

// Append a key/value to an arena-allocated object. Grows capacity by doubling
// using arena allocs (we cannot realloc arena pointers, so we copy on grow).
static void aot_http_obj_set(ObjObject *o, const char *key, int key_len,
                             VMValue val) {
  if (o->count >= o->capacity) {
    int new_cap = o->capacity * 2;
    ObjString **new_keys =
        (ObjString **)aot_arena_alloc(sizeof(ObjString *) * new_cap);
    VMValue *new_vals =
        (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
    memcpy(new_keys, o->keys, sizeof(ObjString *) * o->count);
    memcpy(new_vals, o->values, sizeof(VMValue) * o->count);
    o->keys = new_keys;
    o->values = new_vals;
    o->capacity = new_cap;
  }
  o->keys[o->count] = aot_allocate_string(key, key_len);
  o->values[o->count] = val;
  o->count++;
}

static inline void aot_http_obj_set_str(ObjObject *o, const char *key, int klen,
                                        const char *val, int vlen) {
  ObjString *s = aot_allocate_string(val, vlen);
  aot_http_obj_set(o, key, klen, VM_OBJ((Obj *)s));
}

// URL-decode a percent-escaped fragment in place; returns new length.
static int aot_http_url_decode(const char *src, int slen, char *dst) {
  int o = 0;
  for (int i = 0; i < slen; i++) {
    char c = src[i];
    if (c == '+') {
      dst[o++] = ' ';
    } else if (c == '%' && i + 2 < slen) {
      auto hexv = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
      };
      int hi = hexv(src[i + 1]);
      int lo = hexv(src[i + 2]);
      if (hi >= 0 && lo >= 0) {
        dst[o++] = (char)((hi << 4) | lo);
        i += 2;
      } else {
        dst[o++] = c;
      }
    } else {
      dst[o++] = c;
    }
  }
  return o;
}

// Parse a urlencoded `k1=v1&k2=v2` string into a JSON-style object. Returns a
// fresh ObjObject (always non-null; empty if input is empty/malformed).
static ObjObject *aot_http_parse_query(const char *qs, int qlen) {
  ObjObject *o = aot_http_make_obj(4);
  int i = 0;
  while (i < qlen) {
    int key_start = i;
    while (i < qlen && qs[i] != '=' && qs[i] != '&') i++;
    int key_end = i;
    int val_start = key_end;
    int val_end = key_end;
    if (i < qlen && qs[i] == '=') {
      val_start = i + 1;
      i = val_start;
      while (i < qlen && qs[i] != '&') i++;
      val_end = i;
    }
    if (key_end > key_start) {
      // url-decode key + value into temporary arena buffer.
      char *kbuf = (char *)aot_arena_alloc(key_end - key_start + 1);
      int klen = aot_http_url_decode(qs + key_start, key_end - key_start, kbuf);
      char *vbuf = (char *)aot_arena_alloc((val_end - val_start) + 1);
      int vlen = val_end > val_start
          ? aot_http_url_decode(qs + val_start, val_end - val_start, vbuf)
          : 0;
      aot_http_obj_set_str(o, kbuf, klen, vbuf, vlen);
    }
    if (i < qlen && qs[i] == '&') i++;
  }
  return o;
}

VMValue aot_http_parse_request(VMValue rawRequest) {
  if (!IS_STRING(rawRequest)) {
    return VM_INT(0);
  }

  ObjString *req = AS_STRING(rawRequest);
  const char *data = req->chars;
  int len = req->length;

  ObjObject *result = aot_http_make_obj(8);

  // Parse first line: METHOD PATH HTTP/1.1
  int line_end = 0;
  while (line_end < len && data[line_end] != '\r' && data[line_end] != '\n') {
    line_end++;
  }

  // Method (up to first space)
  int method_end = 0;
  while (method_end < line_end && data[method_end] != ' ') {
    method_end++;
  }
  aot_http_obj_set_str(result, "method", 6, data, method_end);

  // Path (without query)
  int path_start = method_end + 1;
  int path_end = path_start;
  while (path_end < line_end && data[path_end] != ' ' &&
         data[path_end] != '?') {
    path_end++;
  }
  aot_http_obj_set_str(result, "path", 4, data + path_start,
                       path_end - path_start);

  // raw_path (path + query, what the client actually sent)
  int raw_path_end = path_start;
  while (raw_path_end < line_end && data[raw_path_end] != ' ') {
    raw_path_end++;
  }
  aot_http_obj_set_str(result, "raw_path", 8, data + path_start,
                       raw_path_end - path_start);

  // Query: parse `?k=v&...` portion into an object (always present, may be
  // empty). Easier for handlers than asking "did the request have a query?".
  int query_start = path_end;
  int query_end = query_start;
  if (query_start < line_end && data[query_start] == '?') {
    query_start++;
    query_end = query_start;
    while (query_end < line_end && data[query_end] != ' ') {
      query_end++;
    }
  }
  ObjObject *query = aot_http_parse_query(
      data + query_start, query_end > query_start ? query_end - query_start : 0);
  aot_http_obj_set(result, "query", 5, VM_OBJ((Obj *)query));

  // Version (HTTP/1.1 / HTTP/1.0)
  int ver_start = raw_path_end + 1;
  if (ver_start < line_end) {
    aot_http_obj_set_str(result, "version", 7, data + ver_start,
                         line_end - ver_start);
  } else {
    aot_http_obj_set_str(result, "version", 7, "HTTP/1.1", 8);
  }

  // Skip the CR/LF after request line.
  int p = line_end;
  if (p < len && data[p] == '\r') p++;
  if (p < len && data[p] == '\n') p++;

  // Headers — parsed into a nested object. Header names are kept verbatim
  // (case-sensitive lookup mirrors the wire); to do a case-insensitive lookup
  // a wrapper helper can be added later.
  ObjObject *headers = aot_http_make_obj(8);
  int content_length = -1;
  while (p < len) {
    if (data[p] == '\r' || data[p] == '\n') break;
    int h_start = p;
    int colon = -1;
    while (p < len && data[p] != '\r' && data[p] != '\n') {
      if (colon < 0 && data[p] == ':') colon = p;
      p++;
    }
    int h_end = p;
    if (colon > h_start) {
      int key_start = h_start;
      int key_end = colon;
      int val_start = colon + 1;
      while (val_start < h_end && (data[val_start] == ' ' ||
                                   data[val_start] == '\t')) {
        val_start++;
      }
      aot_http_obj_set_str(headers, data + key_start, key_end - key_start,
                           data + val_start, h_end - val_start);
      // Cheap Content-Length capture for accurate body slicing.
      int klen = key_end - key_start;
      if (klen == 14) {
        const char *want = "Content-Length";
        bool ci_match = true;
        for (int j = 0; j < 14; j++) {
          char a = data[key_start + j];
          char b = want[j];
          if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
          if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
          if (a != b) { ci_match = false; break; }
        }
        if (ci_match) {
          char tmp[20];
          int tn = h_end - val_start;
          if (tn > 19) tn = 19;
          memcpy(tmp, data + val_start, tn);
          tmp[tn] = '\0';
          content_length = atoi(tmp);
        }
      }
    }
    if (p < len && data[p] == '\r') p++;
    if (p < len && data[p] == '\n') p++;
  }
  aot_http_obj_set(result, "headers", 7, VM_OBJ((Obj *)headers));

  // Skip the blank line separating headers/body.
  if (p < len && data[p] == '\r') p++;
  if (p < len && data[p] == '\n') p++;

  // Body — bounded by Content-Length when available, otherwise "rest of input".
  int body_len = len - p;
  if (content_length >= 0 && content_length < body_len) {
    body_len = content_length;
  }
  if (body_len > 0) {
    aot_http_obj_set_str(result, "body", 4, data + p, body_len);
  } else {
    aot_http_obj_set_str(result, "body", 4, "", 0);
  }

  return VM_OBJ((Obj *)result);
}

// Map a numeric HTTP status to its IANA reason phrase. Used by the response
// builder and exposed to user code as `http_status_text(N)`.
static const char *aot_http_status_text_cstr(int status) {
  switch (status) {
  case 100: return "Continue";
  case 101: return "Switching Protocols";
  case 200: return "OK";
  case 201: return "Created";
  case 202: return "Accepted";
  case 204: return "No Content";
  case 206: return "Partial Content";
  case 301: return "Moved Permanently";
  case 302: return "Found";
  case 303: return "See Other";
  case 304: return "Not Modified";
  case 307: return "Temporary Redirect";
  case 308: return "Permanent Redirect";
  case 400: return "Bad Request";
  case 401: return "Unauthorized";
  case 402: return "Payment Required";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 406: return "Not Acceptable";
  case 408: return "Request Timeout";
  case 409: return "Conflict";
  case 410: return "Gone";
  case 411: return "Length Required";
  case 413: return "Payload Too Large";
  case 414: return "URI Too Long";
  case 415: return "Unsupported Media Type";
  case 422: return "Unprocessable Entity";
  case 429: return "Too Many Requests";
  case 500: return "Internal Server Error";
  case 501: return "Not Implemented";
  case 502: return "Bad Gateway";
  case 503: return "Service Unavailable";
  case 504: return "Gateway Timeout";
  case 505: return "HTTP Version Not Supported";
  default:  return "OK";
  }
}

// Builtin: exit(int) -> never returns
// Terminates the process with the given exit code. The IR-side caller
// extracts the int payload from VMValue and passes it as a raw i32 so we
// avoid the VMValue ABI dance for a one-shot call. Required by
// lib/test.tpr's test_summary() to fail CI on test failure.
void aot_exit_i32(int code) {
  fflush(stdout);
  fflush(stderr);
  exit(code);
}

// Builtin: http_status_text(int) -> str
// Returns the IANA reason phrase for a numeric HTTP status (e.g. 404 ->
// "Not Found"). Used by API libraries that build responses by hand.
VMValue aot_http_status_text(VMValue statusVal) {
  int status = IS_INT(statusVal) ? (int)AS_INT(statusVal) : 200;
  const char *txt = aot_http_status_text_cstr(status);
  return VM_OBJ((Obj *)aot_allocate_string(txt, (int)strlen(txt)));
}

// Builtin: path_match(pattern, path) -> json
//   Pattern syntax (Express-style):
//     /users          - exact match
//     /users/:id      - capture-named segment ("id" -> "42" in params)
//     /static/*       - wildcard tail (everything after the prefix becomes
//                       params._wildcard; trailing slash optional)
//
//   Returns {"matched": <bool>, "params": <object>}. On no-match, params
//   is empty. The bool is a VMValue bool so user code can do
//   `if (m["matched"]) { ... }`.
//
// This replaces a 50-line Tulpar implementation that ran through the
// interpreter for every incoming request — the native version is ~20x
// faster on the hot path of router-based servers.
VMValue aot_path_match(VMValue patternVal, VMValue pathVal) {
  ObjObject *result = aot_http_make_obj(2);
  ObjObject *params = aot_http_make_obj(4);

  if (!IS_STRING(patternVal) || !IS_STRING(pathVal)) {
    aot_http_obj_set(result, "matched", 7, VM_INT(0));
    aot_http_obj_set(result, "params", 6, VM_OBJ((Obj *)params));
    return VM_OBJ((Obj *)result);
  }
  ObjString *patStr = AS_STRING(patternVal);
  ObjString *pthStr = AS_STRING(pathVal);
  const char *pat = patStr->chars;
  int patLen = patStr->length;
  const char *pth = pthStr->chars;
  int pthLen = pthStr->length;

  // Wildcard tail: pattern ends with "/*"
  if (patLen >= 2 && pat[patLen - 2] == '/' && pat[patLen - 1] == '*') {
    int prefix_len = patLen - 2;
    if (pthLen >= prefix_len && memcmp(pat, pth, prefix_len) == 0) {
      int wc_start = prefix_len;
      // skip leading '/' if present so user gets "foo/bar.css" not "/foo/bar.css"
      if (wc_start < pthLen && pth[wc_start] == '/') wc_start++;
      aot_http_obj_set_str(params, "_wildcard", 9, pth + wc_start,
                           pthLen - wc_start);
      aot_http_obj_set(result, "matched", 7, VM_INT(1));
      aot_http_obj_set(result, "params", 6, VM_OBJ((Obj *)params));
      return VM_OBJ((Obj *)result);
    }
    aot_http_obj_set(result, "matched", 7, VM_INT(0));
    aot_http_obj_set(result, "params", 6, VM_OBJ((Obj *)params));
    return VM_OBJ((Obj *)result);
  }

  // Walk both strings segment by segment. A segment is bounded by '/' or end.
  int pi = 0; // pattern cursor
  int hi = 0; // path cursor
  while (pi < patLen && hi < pthLen) {
    // Both must start a segment with '/' (or both at beginning without).
    if (pat[pi] == '/' && pth[hi] == '/') {
      pi++;
      hi++;
    } else if (pat[pi] == '/' || pth[hi] == '/') {
      // segment boundary mismatch
      aot_http_obj_set(result, "matched", 7, VM_INT(0));
      aot_http_obj_set(result, "params", 6, VM_OBJ((Obj *)params));
      return VM_OBJ((Obj *)result);
    }

    // Find segment ends.
    int p_end = pi;
    while (p_end < patLen && pat[p_end] != '/') p_end++;
    int h_end = hi;
    while (h_end < pthLen && pth[h_end] != '/') h_end++;

    if (p_end > pi && pat[pi] == ':') {
      // Capture: name = pattern[pi+1..p_end]
      if (h_end == hi) {
        // empty path segment, no value to capture
        aot_http_obj_set(result, "matched", 7, VM_INT(0));
        aot_http_obj_set(result, "params", 6, VM_OBJ((Obj *)params));
        return VM_OBJ((Obj *)result);
      }
      aot_http_obj_set_str(params, pat + pi + 1, p_end - pi - 1,
                           pth + hi, h_end - hi);
    } else {
      // Literal segment must match exactly.
      int p_len = p_end - pi;
      int h_len = h_end - hi;
      if (p_len != h_len || memcmp(pat + pi, pth + hi, p_len) != 0) {
        aot_http_obj_set(result, "matched", 7, VM_INT(0));
        aot_http_obj_set(result, "params", 6, VM_OBJ((Obj *)params));
        return VM_OBJ((Obj *)result);
      }
    }
    pi = p_end;
    hi = h_end;
  }

  // Both must be fully consumed (allowing a trailing '/' on either side).
  while (pi < patLen && pat[pi] == '/') pi++;
  while (hi < pthLen && pth[hi] == '/') hi++;
  bool matched = (pi == patLen) && (hi == pthLen);
  aot_http_obj_set(result, "matched", 7, VM_INT(matched ? 1 : 0));
  aot_http_obj_set(result, "params", 6, VM_OBJ((Obj *)params));
  return VM_OBJ((Obj *)result);
}

// wings_find_route(routes, method, path) -> {"index": <int>, "params": <obj>}
//
// Two-pass route lookup matching the prior lib/wings.tpr behaviour
// (`_find_route` + `_find_route_with_params`) but folded into one C
// function so the per-request serve path no longer pays for the
// Tulpar function dispatch + json["method"]/json["path"] hash lookups
// per route iteration.
//
// Pass 1 (exact match): walks every route, returns the first whose
// method AND path equal the request's. The vast majority of static
// routes hit here, never touching the pattern path.
//
// Pass 2 (pattern match): only enters routes whose path contains a
// ':' (param-bearing). Inlines the segment-walking logic that
// `aot_path_match` uses for a single pattern — collapses the
// "walk routes -> for each: walk segments" double loop into one
// function call rather than `routes × path_match()` Tulpar calls.
//
// Return shape mirrors `_find_route_with_params`:
//   {"index": <int>, "params": <obj>}
// `index >= 0` means matched; `params` holds captures from `:name`
// segments (empty object on exact match).
VMValue aot_wings_find_route(VMValue routesVal, VMValue methodVal,
                             VMValue pathVal) {
  ObjObject *result = aot_http_make_obj(2);
  ObjObject *params = aot_http_make_obj(0);

  auto bail = [&](int index_val) -> VMValue {
    aot_http_obj_set(result, "index", 5, VM_INT(index_val));
    aot_http_obj_set(result, "params", 6, VM_OBJ((Obj *)params));
    return VM_OBJ((Obj *)result);
  };

  if (!IS_ARRAY(routesVal) || !IS_STRING(methodVal) || !IS_STRING(pathVal)) {
    return bail(-1);
  }

  ObjArray *routes = (ObjArray *)AS_ARRAY(routesVal);
  ObjString *m = AS_STRING(methodVal);
  ObjString *p = AS_STRING(pathVal);
  const char *mchars = m->chars;
  int mlen = m->length;
  const char *pchars = p->chars;
  int plen = p->length;

  // Pass 1: exact match. Hot path — first route that matches wins.
  // Most production apps have a handful of static routes plus a few
  // `:name`-bearing ones; this loop short-circuits on the static hits.
  for (int i = 0; i < routes->count; i++) {
    VMValue rv = routes->items[i];
    if (!IS_OBJECT(rv)) continue;
    ObjObject *r = (ObjObject *)AS_OBJECT(rv);
    VMValue rm = vm_object_get(r, (char *)"method");
    VMValue rp = vm_object_get(r, (char *)"path");
    if (!IS_STRING(rm) || !IS_STRING(rp)) continue;
    ObjString *rm_s = AS_STRING(rm);
    ObjString *rp_s = AS_STRING(rp);
    if (rm_s->length == mlen && rp_s->length == plen &&
        memcmp(rm_s->chars, mchars, mlen) == 0 &&
        memcmp(rp_s->chars, pchars, plen) == 0) {
      return bail(i);
    }
  }

  // Pass 2: pattern match. We only enter this loop if the exact pass
  // missed entirely, and we only consider routes whose path contains
  // a ':' — non-param routes are exact-only by construction.
  for (int i = 0; i < routes->count; i++) {
    VMValue rv = routes->items[i];
    if (!IS_OBJECT(rv)) continue;
    ObjObject *r = (ObjObject *)AS_OBJECT(rv);
    VMValue rm = vm_object_get(r, (char *)"method");
    VMValue rp = vm_object_get(r, (char *)"path");
    if (!IS_STRING(rm) || !IS_STRING(rp)) continue;
    ObjString *rm_s = AS_STRING(rm);
    ObjString *rp_s = AS_STRING(rp);
    if (rm_s->length != mlen ||
        memcmp(rm_s->chars, mchars, mlen) != 0) continue;
    const char *pat = rp_s->chars;
    int patLen = rp_s->length;
    // Skip non-param routes — already considered above.
    bool has_param = false;
    for (int k = 0; k < patLen; k++) {
      if (pat[k] == ':') { has_param = true; break; }
    }
    if (!has_param) continue;

    // Reset params for this attempt — failed pattern matches must
    // not leak captures into the next route's try (was a real bug
    // when the Tulpar version was first written).
    params = aot_http_make_obj(2);

    // Segment-walk the request path against the pattern path. Same
    // shape as aot_path_match's non-wildcard branch — duplicated
    // here so we don't pay an extra function call per route AND so
    // the captures land in the local `params` object rather than
    // path_match's own allocation we'd have to deep-copy.
    int pi = 0; // pattern cursor
    int hi = 0; // request-path cursor
    bool matched = true;
    while (pi < patLen && hi < plen) {
      if (pat[pi] == '/' && pchars[hi] == '/') { pi++; hi++; continue; }
      if (pat[pi] == '/' || pchars[hi] == '/') { matched = false; break; }
      int p_end = pi;
      while (p_end < patLen && pat[p_end] != '/') p_end++;
      int h_end = hi;
      while (h_end < plen && pchars[h_end] != '/') h_end++;
      if (p_end > pi && pat[pi] == ':') {
        if (h_end == hi) { matched = false; break; }
        aot_http_obj_set_str(params, pat + pi + 1, p_end - pi - 1,
                             pchars + hi, h_end - hi);
      } else {
        int p_len = p_end - pi;
        int h_len = h_end - hi;
        if (p_len != h_len || memcmp(pat + pi, pchars + hi, p_len) != 0) {
          matched = false; break;
        }
      }
      pi = p_end;
      hi = h_end;
    }
    if (matched) {
      // Allow a trailing '/' on either side.
      while (pi < patLen && pat[pi] == '/') pi++;
      while (hi < plen && pchars[hi] == '/') hi++;
      matched = (pi == patLen) && (hi == plen);
    }
    if (matched) {
      return bail(i);
    }
  }

  // No match: return -1 with the (possibly stale-after-failed-pattern)
  // params object empty. Rebuilt for cleanliness.
  params = aot_http_make_obj(0);
  return bail(-1);
}

// Builtin: parse_cookies(str) -> json
// Parse an RFC 6265 Cookie header value (`a=1; b=2; c=hello`) into a
// JSON-style object. Pairs are split on `;`, leading whitespace on each
// pair is trimmed, and the first `=` separates name from value (so a
// value containing `=` round-trips intact). Empty names are skipped;
// duplicate names follow last-write-wins. No URL-decoding — cookies use
// quoted-pair / token grammar, not percent-encoding, and decoding here
// would corrupt quoted strings.
VMValue aot_parse_cookies(VMValue strVal) {
  if (!IS_STRING(strVal)) {
    return VM_OBJ((Obj *)aot_http_make_obj(0));
  }
  ObjString *s = AS_STRING(strVal);
  const char *data = s->chars;
  int len = s->length;
  ObjObject *o = aot_http_make_obj(4);
  int i = 0;
  while (i < len) {
    while (i < len && (data[i] == ' ' || data[i] == '\t')) i++;
    int key_start = i;
    while (i < len && data[i] != '=' && data[i] != ';') i++;
    int key_end = i;
    int val_start = key_end;
    int val_end = key_end;
    if (i < len && data[i] == '=') {
      val_start = i + 1;
      i = val_start;
      while (i < len && data[i] != ';') i++;
      val_end = i;
    }
    if (key_end > key_start) {
      aot_http_obj_set_str(o, data + key_start, key_end - key_start,
                           data + val_start, val_end - val_start);
    }
    if (i < len && data[i] == ';') i++;
  }
  return VM_OBJ((Obj *)o);
}

// Builtin: parse_query(str) -> json
// Parse a urlencoded `k1=v1&k2=v2` string (with or without a leading '?')
// into a JSON-style object. Same internals as the parser used for HTTP
// request query strings, exposed for user-space body parsing.
// ---------------------------------------------------------------------------
// Crypto / encoding utilities — sha1 + base64.
//
// Self-contained implementations (no OpenSSL dependency) so the helpers
// are always available, regardless of whether the build pulled OpenSSL
// in for HTTPS. Used by the WebSocket accept-key derivation
// (`wings_ws_accept_key`), but also broadly useful for cookies, JWT
// HMAC, ETags, content-addressed caches — anything that wants
// "give me a stable short hash of this blob" or "embed binary in a
// header line".
// ---------------------------------------------------------------------------

namespace {

void sha1_compute(const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE,
                    0x10325476, 0xC3D2E1F0};
    // Pad: append 0x80, zeros, then 64-bit big-endian length-in-bits.
    size_t padded_len = ((len + 9 + 63) / 64) * 64;
    std::vector<uint8_t> buf(padded_len, 0);
    std::memcpy(buf.data(), data, len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        buf[padded_len - 1 - i] = (uint8_t)((bits >> (i * 8)) & 0xFF);
    }
    for (size_t off = 0; off < padded_len; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)buf[off + i * 4 + 0] << 24) |
                   ((uint32_t)buf[off + i * 4 + 1] << 16) |
                   ((uint32_t)buf[off + i * 4 + 2] << 8)  |
                   ((uint32_t)buf[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++) {
            uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (v << 1) | (v >> 31);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++) {
        out[i * 4 + 0] = (uint8_t)((h[i] >> 24) & 0xFF);
        out[i * 4 + 1] = (uint8_t)((h[i] >> 16) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((h[i] >> 8)  & 0xFF);
        out[i * 4 + 3] = (uint8_t)(h[i]         & 0xFF);
    }
}

void base64_encode_buf(const uint8_t *src, size_t len, std::string &out) {
    static const char alpha[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    out.clear();
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) |
                     (uint32_t)src[i + 2];
        out.push_back(alpha[(v >> 18) & 63]);
        out.push_back(alpha[(v >> 12) & 63]);
        out.push_back(alpha[(v >> 6) & 63]);
        out.push_back(alpha[v & 63]);
        i += 3;
    }
    if (i < len) {
        uint32_t v = (uint32_t)src[i] << 16;
        int rem = (int)(len - i);
        if (rem == 2) v |= (uint32_t)src[i + 1] << 8;
        out.push_back(alpha[(v >> 18) & 63]);
        out.push_back(alpha[(v >> 12) & 63]);
        out.push_back(rem == 2 ? alpha[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
}

bool base64_decode_buf(const char *src, size_t len, std::string &out) {
    static int8_t table[256];
    static bool table_inited = false;
    if (!table_inited) {
        for (int i = 0; i < 256; i++) table[i] = -1;
        const char *a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++) table[(uint8_t)a[i]] = (int8_t)i;
        table_inited = true;
    }
    out.clear();
    out.reserve((len * 3) / 4);
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)src[i];
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int v = table[c];
        if (v < 0) return false;
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xFF));
        }
    }
    return true;
}

}  // namespace

// sha1(str) -> 20-byte binary string (each byte is one char in the
// returned string). Useful as a building block; user code wanting a
// printable hash should call sha1_hex.
VMValue aot_sha1(VMValue strVal) {
    if (!IS_STRING(strVal)) return VM_OBJ((Obj *)aot_allocate_string("", 0));
    ObjString *s = AS_STRING(strVal);
    uint8_t digest[20];
    sha1_compute((const uint8_t *)s->chars, (size_t)s->length, digest);
    return VM_OBJ((Obj *)aot_allocate_string((const char *)digest, 20));
}

// sha1_hex(str) -> 40-char lowercase hex digest.
VMValue aot_sha1_hex(VMValue strVal) {
    if (!IS_STRING(strVal)) return VM_OBJ((Obj *)aot_allocate_string("", 0));
    ObjString *s = AS_STRING(strVal);
    uint8_t digest[20];
    sha1_compute((const uint8_t *)s->chars, (size_t)s->length, digest);
    char hex[41];
    for (int i = 0; i < 20; i++) {
        std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return VM_OBJ((Obj *)aot_allocate_string(hex, 40));
}

// base64_encode(str) -> base64 string. Handles arbitrary bytes (including
// the binary 20-byte form returned by sha1()); padding (`=`) is emitted
// when the input length isn't a multiple of 3.
VMValue aot_base64_encode(VMValue strVal) {
    if (!IS_STRING(strVal)) return VM_OBJ((Obj *)aot_allocate_string("", 0));
    ObjString *s = AS_STRING(strVal);
    std::string out;
    base64_encode_buf((const uint8_t *)s->chars, (size_t)s->length, out);
    return VM_OBJ((Obj *)aot_allocate_string(out.data(), (int)out.size()));
}

// base64_decode(str) -> decoded bytes as a string. Whitespace inside
// the input is skipped (so callers can pass pretty-printed base64).
// Returns empty string on malformed input.
VMValue aot_base64_decode(VMValue strVal) {
    if (!IS_STRING(strVal)) return VM_OBJ((Obj *)aot_allocate_string("", 0));
    ObjString *s = AS_STRING(strVal);
    std::string out;
    if (!base64_decode_buf(s->chars, (size_t)s->length, out)) {
        return VM_OBJ((Obj *)aot_allocate_string("", 0));
    }
    return VM_OBJ((Obj *)aot_allocate_string(out.data(), (int)out.size()));
}

// wings_ws_accept_key(client_key) -> base64(sha1(client_key + GUID))
//
// The WebSocket upgrade handshake (RFC 6455 §4.2.2 step 5) demands the
// server prove it parsed the Sec-WebSocket-Key header by returning
// `base64(sha1(<key> + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`. This
// helper is the only piece of the upgrade that benefits from a native:
// the rest (header parse, response build, frame masking) is pure
// string manipulation that's fine in Tulpar source.
// Per-thread current-request fd. Wings sets this on every incoming
// request before invoking the user's handler; streaming handlers
// (SSE / WS upgrade / long-poll) read it back via
// `wings_current_fd()` so they can write to the socket directly,
// then signal `{"_stream": 1}` to tell the dispatcher "I already
// owned the response; skip the envelope build".
//
// Stored as a C-level thread-local rather than a Tulpar
// `_request[k] = client` slot because the latter shape collides
// with the still-open wings cookies miscompile (a `_request[k] = …`
// write followed by another subscript write corrupts the object).
static TULPAR_TLS int64_t g_wings_current_fd = 0;

VMValue aot_wings_set_current_fd(VMValue fdVal) {
    if (IS_INT(fdVal)) g_wings_current_fd = AS_INT(fdVal);
    return VM_INT(0);
}

VMValue aot_wings_current_fd(void) {
    return VM_INT(g_wings_current_fd);
}

VMValue aot_wings_ws_accept_key(VMValue keyVal) {
    if (!IS_STRING(keyVal)) return VM_OBJ((Obj *)aot_allocate_string("", 0));
    ObjString *s = AS_STRING(keyVal);
    std::string combined;
    combined.assign(s->chars, (size_t)s->length);
    combined += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[20];
    sha1_compute((const uint8_t *)combined.data(), combined.size(), digest);
    std::string out;
    base64_encode_buf(digest, 20, out);
    return VM_OBJ((Obj *)aot_allocate_string(out.data(), (int)out.size()));
}

// ---------------------------------------------------------------------------
// WebSocket frame I/O — `wings_ws_send_frame` + `wings_ws_recv_frame`.
//
// RFC 6455 §5.2 frame layout:
//
//   bit 0:    FIN (we always set: single-message frames only)
//   bit 1-3:  RSV1-3 (always 0)
//   bit 4-7:  opcode (1=text, 2=binary, 8=close, 9=ping, 10=pong)
//   bit 8:    MASK (server→client unmasked = 0; client→server = 1)
//   bit 9-15: payload-length
//     < 126:  this IS the length
//     126:    next 2 bytes big-endian unsigned length
//     127:    next 8 bytes big-endian unsigned length
//   masking-key (4 bytes) — only when MASK=1
//   payload (length bytes; XOR'd with masking-key when MASK=1)
//
// Both helpers do synchronous reads/writes via `recv`/`send`. Loops
// guard against short reads (TCP can split header + length + key
// across multiple TCP segments under load). Caller owns the fd; this
// helper just sees a connected socket.
// ---------------------------------------------------------------------------

namespace {

// recv exactly N bytes, looping past short reads. Returns false if
// the peer closed cleanly or errored before N bytes arrived.
bool ws_recv_exact(tulpar_socket fd, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = (ssize_t)tulpar_recv(fd, (char *)buf + got,
                                         (int)(n - got), 0);
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

}  // namespace

// wings_ws_send_frame(fd, opcode, payload) -> int (bytes sent or -1)
//
// Server→client: always unmasked per RFC 6455 §5.1. We always set FIN
// (single-frame messages) — fragmented sends require multiple calls
// with opcode 0 for continuations and aren't useful for typical
// JSON-line / text-event traffic. Caller may construct any opcode
// (text=1, binary=2, close=8, ping=9, pong=10); we don't enforce
// "control frames must be ≤125 bytes" — that's the caller's job.
VMValue aot_wings_ws_send_frame(VMValue fdVal, VMValue opcodeVal,
                                VMValue payloadVal) {
    if (!IS_INT(fdVal) || !IS_INT(opcodeVal) || !IS_STRING(payloadVal)) {
        return VM_INT(-1);
    }
    tulpar_socket fd = (tulpar_socket)AS_INT(fdVal);
    int opcode = (int)AS_INT(opcodeVal) & 0x0F;
    ObjString *p = AS_STRING(payloadVal);
    // ObjString.length is int, so promotion to size_t can't go negative;
    // make that explicit so memcpy's bound-check doesn't warn.
    if (p->length < 0) return VM_INT(-1);
    size_t len = (size_t)p->length;

    // Pre-size a header buffer big enough for the worst case:
    // 1 (FIN|opcode) + 1 (mask|len-prefix) + 8 (64-bit ext length).
    uint8_t header[10];
    int hlen = 0;
    header[hlen++] = (uint8_t)(0x80 | opcode);  // FIN + opcode
    if (len < 126) {
        header[hlen++] = (uint8_t)len;
    } else if (len <= 0xFFFF) {
        header[hlen++] = 126;
        header[hlen++] = (uint8_t)((len >> 8) & 0xFF);
        header[hlen++] = (uint8_t)(len & 0xFF);
    } else {
        header[hlen++] = 127;
        uint64_t u64 = (uint64_t)len;
        for (int i = 7; i >= 0; i--) {
            header[hlen++] = (uint8_t)((u64 >> (i * 8)) & 0xFF);
        }
    }

    // Send header + payload as one contiguous buffer. Two separate
    // send() calls would also work in principle, but on Windows the
    // pair occasionally surfaces an ordering quirk where the second
    // segment doesn't reach the peer for an unusually long time —
    // most visible in tests that read back-to-back frames. Buffer
    // copy cost is negligible against the syscall savings.
    std::vector<uint8_t> wire((size_t)hlen + len);
    std::memcpy(wire.data(), header, (size_t)hlen);
    if (len > 0) {
        std::memcpy(wire.data() + hlen, p->chars, len);
    }
    size_t sent = 0;
    size_t total = wire.size();
    while (sent < total) {
        ssize_t s = (ssize_t)tulpar_send(fd, (const char *)wire.data() + sent,
                                         (int)(total - sent), 0);
        if (s < 0) return VM_INT(-1);
        sent += (size_t)s;
    }
    return VM_INT((int64_t)total);
}

// wings_ws_recv_frame(fd) -> json
//
// Returns an object:
//   { "ok": 1, "opcode": <int>, "fin": <int>, "payload": <str> }
// or on disconnect / malformed input:
//   { "ok": 0, "error": "<reason>" }
//
// Payload is unmasked when the client sets the MASK bit (which it
// must per RFC 6455 §5.3 — client→server frames are always masked).
// We accept unmasked frames too in case Tulpar ever does the
// non-standard "act as a WebSocket client" role; the wire is
// otherwise identical.
//
// Caps payload at 16 MiB to keep a hostile client from triggering
// unbounded malloc; that's well above the typical JSON-line / chat
// message size and matches `_wings_max_body_bytes`'s default ceiling.
VMValue aot_wings_ws_recv_frame(VMValue fdVal) {
    auto err = [](const char *msg) -> VMValue {
        ObjObject *r = aot_http_make_obj(2);
        aot_http_obj_set(r, "ok", 2, VM_INT(0));
        aot_http_obj_set_str(r, "error", 5, msg, (int)strlen(msg));
        return VM_OBJ((Obj *)r);
    };
    if (!IS_INT(fdVal)) return err("bad fd");
    tulpar_socket fd = (tulpar_socket)AS_INT(fdVal);

    uint8_t hdr[2];
    if (!ws_recv_exact(fd, hdr, 2)) return err("connection closed");
    int fin = (hdr[0] >> 7) & 1;
    int opcode = hdr[0] & 0x0F;
    int masked = (hdr[1] >> 7) & 1;
    uint64_t payload_len = hdr[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t ext[2];
        if (!ws_recv_exact(fd, ext, 2)) return err("short read on 16-bit length");
        payload_len = ((uint64_t)ext[0] << 8) | (uint64_t)ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (!ws_recv_exact(fd, ext, 8)) return err("short read on 64-bit length");
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | (uint64_t)ext[i];
        }
    }

    if (payload_len > 16 * 1024 * 1024ULL) {
        return err("payload exceeds 16 MiB cap");
    }

    uint8_t mask_key[4] = {0, 0, 0, 0};
    if (masked) {
        if (!ws_recv_exact(fd, mask_key, 4)) return err("short read on mask key");
    }

    std::vector<char> payload((size_t)payload_len);
    if (payload_len > 0) {
        if (!ws_recv_exact(fd, (uint8_t *)payload.data(),
                           (size_t)payload_len)) {
            return err("short read on payload");
        }
        if (masked) {
            for (uint64_t i = 0; i < payload_len; i++) {
                payload[(size_t)i] ^= (char)mask_key[i & 3];
            }
        }
    }

    ObjObject *r = aot_http_make_obj(4);
    aot_http_obj_set(r, "ok", 2, VM_INT(1));
    aot_http_obj_set(r, "opcode", 6, VM_INT(opcode));
    aot_http_obj_set(r, "fin", 3, VM_INT(fin));
    aot_http_obj_set_str(r, "payload", 7, payload.data(), (int)payload_len);
    return VM_OBJ((Obj *)r);
}

VMValue aot_parse_query(VMValue strVal) {
  if (!IS_STRING(strVal)) {
    return VM_OBJ((Obj *)aot_http_make_obj(0));
  }
  ObjString *s = AS_STRING(strVal);
  const char *data = s->chars;
  int len = s->length;
  // Trim a leading '?' so callers can pass either the raw query string or
  // the same form `?k=v` they got from a URL.
  if (len > 0 && data[0] == '?') {
    data++;
    len--;
  }
  ObjObject *o = aot_http_parse_query(data, len);
  return VM_OBJ((Obj *)o);
}

// ---- multipart/form-data parsing ------------------------------------------
// Binary-safe substring search (bodies carry raw file bytes incl. NULs).
static const char *aot_memfind(const char *hay, int haylen, const char *needle,
                               int nlen) {
  if (nlen <= 0 || haylen < nlen)
    return nullptr;
  for (int i = 0; i + nlen <= haylen; i++) {
    if (memcmp(hay + i, needle, nlen) == 0)
      return hay + i;
  }
  return nullptr;
}

// Extract a `key="..."` attribute value from [buf, buf+len). `key` includes the
// trailing `="`. Copies up to cap-1 bytes into out (NUL-terminated), returns the
// length, or 0 if absent.
static int aot_mp_attr(const char *buf, int len, const char *key, int klen,
                       char *out, int cap) {
  const char *k = aot_memfind(buf, len, key, klen);
  if (!k)
    return 0;
  const char *v = k + klen;
  int rem = (int)(buf + len - v);
  int i = 0;
  while (i < rem && v[i] != '"' && i < cap - 1) {
    out[i] = v[i];
    i++;
  }
  out[i] = 0;
  return i;
}

// aot_parse_multipart(body, content_type) ->
//   { "fields": {name: value, ...},
//     "files":  [{name, filename, content_type, data, size}, ...] }
// Text parts land in `fields`; parts with a filename land in `files` with their
// raw bytes (binary-safe — arena strings are length-tracked, not strlen).
VMValue aot_parse_multipart(VMValue body_val, VMValue ct_val) {
  ObjObject *result = aot_http_make_obj(2);
  ObjObject *fields = aot_http_make_obj(4);
  ObjArray *files = vm_allocate_array_aot_wrapper(nullptr);
  aot_http_obj_set(result, "fields", 6, VM_OBJ((Obj *)fields));
  aot_http_obj_set(result, "files", 5, VM_OBJ((Obj *)files));

  if (!IS_STRING(body_val) || !IS_STRING(ct_val))
    return VM_OBJ((Obj *)result);

  const char *ct = AS_STRING(ct_val)->chars;
  int ctlen = AS_STRING(ct_val)->length;
  const char *body = AS_STRING(body_val)->chars;
  int blen = AS_STRING(body_val)->length;

  // boundary= from the content-type header (may be quoted / followed by ;).
  const char *bp = aot_memfind(ct, ctlen, "boundary=", 9);
  if (!bp)
    return VM_OBJ((Obj *)result);
  const char *bv = bp + 9;
  int bvlen = (int)(ct + ctlen - bv);
  if (bvlen > 0 && bv[0] == '"') {
    bv++;
    bvlen--;
  }
  int be = 0;
  while (be < bvlen && bv[be] != '"' && bv[be] != ';' && bv[be] != '\r' &&
         bv[be] != '\n' && bv[be] != ' ')
    be++;
  bvlen = be;
  if (bvlen <= 0 || bvlen > 250)
    return VM_OBJ((Obj *)result);

  // delimiter = "--" + boundary
  char delim[256];
  delim[0] = '-';
  delim[1] = '-';
  memcpy(delim + 2, bv, bvlen);
  int dlen = bvlen + 2;

  const char *p = aot_memfind(body, blen, delim, dlen);
  while (p) {
    p += dlen;
    int rem = (int)(body + blen - p);
    if (rem >= 2 && p[0] == '-' && p[1] == '-')
      break; // closing "--boundary--"
    if (rem >= 2 && p[0] == '\r' && p[1] == '\n')
      p += 2;
    else if (rem >= 1 && p[0] == '\n')
      p += 1;

    const char *next = aot_memfind(p, (int)(body + blen - p), delim, dlen);
    if (!next)
      break;
    int partlen = (int)(next - p);

    const char *he = aot_memfind(p, partlen, "\r\n\r\n", 4);
    if (he) {
      int headers_len = (int)(he - p);
      const char *content = he + 4;
      int content_len = (int)(next - content);
      if (content_len >= 2 && content[content_len - 2] == '\r' &&
          content[content_len - 1] == '\n')
        content_len -= 2; // strip the CRLF before the next delimiter

      char name[256];
      int namelen = aot_mp_attr(p, headers_len, "name=\"", 6, name, sizeof(name));
      char fname[256];
      int fnamelen =
          aot_mp_attr(p, headers_len, "filename=\"", 10, fname, sizeof(fname));

      // Optional per-part Content-Type.
      char pct[128];
      int pctlen = 0;
      const char *ctp = aot_memfind(p, headers_len, "Content-Type:", 13);
      if (ctp) {
        const char *cv = ctp + 13;
        int crem = (int)(p + headers_len - cv);
        while (crem > 0 && (*cv == ' ' || *cv == '\t')) {
          cv++;
          crem--;
        }
        while (pctlen < crem && cv[pctlen] != '\r' && cv[pctlen] != '\n' &&
               pctlen < (int)sizeof(pct) - 1) {
          pct[pctlen] = cv[pctlen];
          pctlen++;
        }
      }

      if (fnamelen > 0) {
        ObjObject *fo = aot_http_make_obj(5);
        aot_http_obj_set_str(fo, "name", 4, name, namelen);
        aot_http_obj_set_str(fo, "filename", 8, fname, fnamelen);
        aot_http_obj_set_str(fo, "content_type", 12, pct, pctlen);
        ObjString *data = aot_allocate_string(content, content_len);
        aot_http_obj_set(fo, "data", 4, VM_OBJ((Obj *)data));
        aot_http_obj_set(fo, "size", 4, VM_INT(content_len));
        vm_array_push_aot_wrapper(nullptr, files, VM_OBJ((Obj *)fo));
      } else if (namelen > 0) {
        aot_http_obj_set_str(fields, name, namelen, content, content_len);
      }
    }
    p = next;
  }
  return VM_OBJ((Obj *)result);
}

// Pointer-ABI wrapper (matches aot_split_ptr): the AOT backend passes VMValue
// struct args by pointer to dodge the SysV/MinGW struct-arg payload drop.
VMValue aot_parse_multipart_ptr(VMValue *body_ptr, VMValue *ct_ptr) {
  if (!body_ptr || !ct_ptr)
    return VM_VOID();
  return aot_parse_multipart(*body_ptr, *ct_ptr);
}

// Builtin: http_create_response_full(status, content_type, body, headers)
// Same as http_create_response but accepts a JSON object of extra headers
// (e.g. {"Access-Control-Allow-Origin": "*", "Set-Cookie": "..."}). Each
// key/value is emitted on its own header line. Required for any real-world
// API that needs CORS, cookies, cache directives, or auth challenges —
// without it the only way to set headers was string-concat by hand.
VMValue aot_http_create_response_full(VMValue statusVal, VMValue contentTypeVal,
                                      VMValue bodyVal, VMValue headersVal) {
  int status = IS_INT(statusVal) ? (int)AS_INT(statusVal) : 200;
  const char *content_type = IS_STRING(contentTypeVal)
                                 ? AS_STRING(contentTypeVal)->chars
                                 : "text/plain";
  const char *body = "";
  int body_len = 0;
  if (IS_STRING(bodyVal)) {
    body = AS_STRING(bodyVal)->chars;
    body_len = AS_STRING(bodyVal)->length;
  }
  const char *status_text = aot_http_status_text_cstr(status);

  // Compose extra headers (skip Content-Type / Content-Length / Connection
  // since we set those ourselves below; user-supplied versions would
  // duplicate and confuse clients).
  ObjObject *headers = nullptr;
  if (IS_OBJECT(headersVal)) {
    headers = (ObjObject *)AS_OBJECT(headersVal);
  }
  size_t extra_len = 0;
  if (headers) {
    for (int i = 0; i < headers->count; i++) {
      ObjString *k = headers->keys[i];
      ObjString *v = (IS_STRING(headers->values[i]))
                         ? AS_STRING(headers->values[i])
                         : nullptr;
      if (!k || !v) continue;
      const char *kn = k->chars;
      // Skip keys we own.
      auto eq_ci = [](const char *a, const char *b) {
        while (*a && *b) {
          char ca = *a++, cb = *b++;
          if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
          if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
          if (ca != cb) return false;
        }
        return *a == 0 && *b == 0;
      };
      if (eq_ci(kn, "Content-Type") || eq_ci(kn, "Content-Length") ||
          eq_ci(kn, "Connection")) continue;
      extra_len += k->length + 2 /*': '*/ + v->length + 2 /*\r\n*/;
    }
  }

  // Worst-case response size: status line + 4 standard headers + extras + body.
  size_t cap = 80 + strlen(content_type) + extra_len + body_len + 64;
  char *response = (char *)aot_arena_alloc(cap + 1);
  int p = snprintf(response, cap,
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %d\r\n",
                   status, status_text, content_type, body_len);
  if (headers) {
    for (int i = 0; i < headers->count; i++) {
      ObjString *k = headers->keys[i];
      VMValue vv = headers->values[i];
      if (!k || !IS_STRING(vv)) continue;
      ObjString *v = AS_STRING(vv);
      const char *kn = k->chars;
      auto eq_ci = [](const char *a, const char *b) {
        while (*a && *b) {
          char ca = *a++, cb = *b++;
          if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
          if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
          if (ca != cb) return false;
        }
        return *a == 0 && *b == 0;
      };
      if (eq_ci(kn, "Content-Type") || eq_ci(kn, "Content-Length") ||
          eq_ci(kn, "Connection")) continue;
      p += snprintf(response + p, cap - p, "%s: %.*s\r\n", kn, v->length,
                    v->chars);
    }
  }
  p += snprintf(response + p, cap - p, "Connection: close\r\n\r\n");
  if (body_len > 0 && (size_t)p + body_len < cap) {
    memcpy(response + p, body, body_len);
    p += body_len;
  }
  response[p] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(response, p));
}

// ----- Case-insensitive object key lookup -----
//
// Used by `http_should_keepalive` to find the request's `Connection`
// header without caring about wire-case ("connection" / "Connection" /
// "CONNECTION" all hit). `name_len` lets the caller pass a string
// literal length without strlen.
static VMValue aot_http_obj_get_ci(ObjObject *o, const char *name,
                                   int name_len) {
  if (!o) return VM_INT(0);
  for (int i = 0; i < o->count; i++) {
    ObjString *k = o->keys[i];
    if (!k || k->length != name_len) continue;
    bool eq = true;
    for (int j = 0; j < name_len; j++) {
      char a = k->chars[j];
      char b = name[j];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
      if (a != b) { eq = false; break; }
    }
    if (eq) return o->values[i];
  }
  return VM_INT(0);
}

// Builtin: http_should_keepalive(parsed_request) -> int (0/1)
//
// Honors HTTP/1.1 default keep-alive and HTTP/1.0 default close, both
// overridable by an explicit `Connection:` header. Tulpar stdlib's
// listen() loop calls this once per request to decide whether to
// re-receive on the same fd or close it.
VMValue aot_http_should_keepalive(VMValue requestVal) {
  if (!IS_OBJECT(requestVal)) return VM_INT(0);
  ObjObject *req = (ObjObject *)AS_OBJECT(requestVal);

  bool http11 = true;
  VMValue v = aot_http_obj_get_ci(req, "version", 7);
  if (IS_STRING(v)) {
    ObjString *vs = AS_STRING(v);
    if (vs->length == 8 && memcmp(vs->chars, "HTTP/1.0", 8) == 0) {
      http11 = false;
    }
  }

  VMValue h = aot_http_obj_get_ci(req, "headers", 7);
  if (IS_OBJECT(h)) {
    VMValue conn = aot_http_obj_get_ci((ObjObject *)AS_OBJECT(h),
                                       "Connection", 10);
    if (IS_STRING(conn)) {
      ObjString *cs = AS_STRING(conn);
      auto contains_ci = [&](const char *needle) {
        int nlen = (int)strlen(needle);
        for (int i = 0; i + nlen <= cs->length; i++) {
          bool eq = true;
          for (int j = 0; j < nlen; j++) {
            char a = cs->chars[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) { eq = false; break; }
          }
          if (eq) return true;
        }
        return false;
      };
      if (contains_ci("close")) return VM_INT(0);
      if (contains_ci("keep-alive")) return VM_INT(1);
    }
  }
  return VM_INT(http11 ? 1 : 0);
}

// Builtin: http_recv_request(client_fd, max_bytes) -> str
//
// Reads exactly one HTTP/1.x request off `client_fd`. `max_bytes` is the
// **absolute upper bound** (DoS guard) — request size grows the receive
// buffer dynamically up to that ceiling, never beyond it. Returns ""
// on connection close, error, or if the announced Content-Length would
// exceed `max_bytes`.
//
// Algorithm:
//   1. Recv into a 64KB thread-local static buffer until `\r\n\r\n` is
//      seen — that block is plenty for the largest realistic header
//      block (browsers cap themselves around 8KB; we leave headroom).
//   2. Parse `Content-Length:` out of the headers.
//   3. If `needed = header_end + content_length` exceeds `max_bytes`,
//      reject (return ""). Otherwise, if `needed` exceeds the static
//      buffer's 64KB capacity, switch to a heap-allocated buffer
//      sized to `needed + 1` and copy across what we already read.
//   4. Continue receiving until total >= needed, then return.
//
// We deliberately don't consume bytes past `needed` so a future
// `http_recv_request` on the same fd starts cleanly.
//
// Reusing the static buffer across calls (when the request fits) saves
// a 64KB malloc + free on every keep-alive request — measurable on
// hot-path benchmarks. The heap fallback only kicks in for body
// payloads larger than 64KB (file uploads, big JSON, etc.), which is
// when the cost of the malloc is dwarfed by the bytes themselves.
VMValue aot_http_recv_request(VMValue fdVal, VMValue maxVal) {
  if (!IS_INT(fdVal))
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  tulpar_socket fd = (tulpar_socket)AS_INT(fdVal);
  // Default cap: 16 MiB. Big enough for realistic API uploads (package
  // tarballs, multi-record JSON imports), small enough that a single
  // worker can't exhaust memory under attack.
  int max_bytes = IS_INT(maxVal) ? (int)AS_INT(maxVal) : 16 * 1024 * 1024;
  if (max_bytes < 1024) max_bytes = 1024;

  static const int STATIC_CAP = 65536;
  static thread_local char *static_buf = nullptr;
  if (!static_buf) {
    static_buf = (char *)malloc(STATIC_CAP + 1);
  }
  char *buf = static_buf;
  int cap = static_buf ? STATIC_CAP : 0;
  bool buf_owned = false;
  if (!buf) {
    // First-call malloc failed — fall back to per-call alloc capped at
    // either max_bytes or STATIC_CAP, whichever is smaller, so we still
    // make progress on the header read.
    int initial = max_bytes < STATIC_CAP ? max_bytes : STATIC_CAP;
    buf = (char *)malloc(initial + 1);
    if (!buf) return VM_OBJ((Obj *)aot_allocate_string("", 0));
    cap = initial;
    buf_owned = true;
  }

  int total = 0;
  int header_end = -1;
  int content_length = -1;
  int needed = -1;

  for (;;) {
    // Read window: until we know `needed`, fill up to `cap`. After that,
    // fill up to `needed`. Either way, `cap` is the hard physical bound.
    int target = (needed > 0) ? needed : cap;
    if (target > cap) target = cap; // belt-and-braces
    if (total >= target) break;

    ssize_t n = tulpar_recv(fd, buf + total, target - total, 0);
    if (n <= 0) {
      if (buf_owned) free(buf);
      return VM_OBJ((Obj *)aot_allocate_string("", 0));
    }
    total += (int)n;

    if (header_end < 0) {
      int scan_from = total - (int)n - 3;
      if (scan_from < 0) scan_from = 0;
      for (int i = scan_from; i + 3 < total; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
          header_end = i + 4;
          break;
        }
      }
      if (header_end < 0) {
        // Header didn't fit in `cap` bytes — pathological client; reject.
        if (total >= cap) {
          if (buf_owned) free(buf);
          return VM_OBJ((Obj *)aot_allocate_string("", 0));
        }
        continue;
      }

      content_length = 0;
      const char *hdr_end = buf + header_end - 4;
      const char *line_start = buf;
      const char *prefix = "Content-Length:";
      int plen = 15;
      while (line_start < hdr_end) {
        const char *line_end = line_start;
        while (line_end < hdr_end && *line_end != '\r' && *line_end != '\n')
          line_end++;
        if (line_end - line_start > plen) {
          bool m = true;
          for (int j = 0; j < plen; j++) {
            char a = line_start[j], b = prefix[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) { m = false; break; }
          }
          if (m) {
            const char *p = line_start + plen;
            while (p < line_end && (*p == ' ' || *p == '\t')) p++;
            long long val = 0;
            while (p < line_end && *p >= '0' && *p <= '9') {
              val = val * 10 + (*p - '0');
              p++;
            }
            // Clamp to int range so the cap math below stays safe.
            if (val > 0x7fffffffLL) val = 0x7fffffffLL;
            content_length = (int)val;
            break;
          }
        }
        line_start = line_end;
        if (line_start < hdr_end && *line_start == '\r') line_start++;
        if (line_start < hdr_end && *line_start == '\n') line_start++;
      }
      needed = header_end + content_length;

      // DoS guard: announced body would push us past max_bytes.
      if (needed > max_bytes) {
        if (buf_owned) free(buf);
        return VM_OBJ((Obj *)aot_allocate_string("", 0));
      }

      // Body too big for the static buffer → grow into a private heap
      // allocation. Copy what we already received so the recv loop can
      // continue from `total` without restarting.
      if (needed > cap) {
        char *grown = (char *)malloc((size_t)needed + 1);
        if (!grown) {
          if (buf_owned) free(buf);
          return VM_OBJ((Obj *)aot_allocate_string("", 0));
        }
        memcpy(grown, buf, (size_t)total);
        if (buf_owned) free(buf);
        buf = grown;
        cap = needed;
        buf_owned = true;
      }
    }

    if (needed > 0 && total >= needed) break;
  }

  buf[total] = '\0';
  ObjString *res = aot_allocate_string(buf, total);
  if (buf_owned) free(buf);
  return VM_OBJ((Obj *)res);
}

// Builtin: http_create_response_keepalive(status, ct, body, headers, keep)
//
// Identical to http_create_response_full(4-arg) except the Connection
// header follows the `keep` flag (1 -> "keep-alive", 0 -> "close")
// instead of being hardcoded. Existing 3- and 4-arg call sites stay
// unchanged for backwards compat; only stdlib (wings.tpr / router.tpr)
// upgrades to this 5-arg form.
// Fast unsigned int -> ASCII conversion. Hand-rolled because snprintf's
// format-string interpretation is measurably costly on a hot HTTP path
// (40-100ns per call × 4 calls per response). Returns the advanced
// write pointer.
static inline char *fast_u32_itoa(char *dst, uint32_t n) {
  if (n == 0) { *dst++ = '0'; return dst; }
  char tmp[12];
  int i = 0;
  while (n > 0) {
    tmp[i++] = (char)('0' + (n % 10));
    n /= 10;
  }
  while (i > 0) *dst++ = tmp[--i];
  return dst;
}

VMValue aot_http_create_response_keepalive(VMValue statusVal,
                                           VMValue contentTypeVal,
                                           VMValue bodyVal,
                                           VMValue headersVal,
                                           VMValue keepVal) {
  int status = IS_INT(statusVal) ? (int)AS_INT(statusVal) : 200;
  const char *content_type = IS_STRING(contentTypeVal)
                                 ? AS_STRING(contentTypeVal)->chars
                                 : "text/plain";
  size_t content_type_len = IS_STRING(contentTypeVal)
                                ? (size_t)AS_STRING(contentTypeVal)->length
                                : strlen("text/plain");
  const char *body = "";
  int body_len = 0;
  if (IS_STRING(bodyVal)) {
    body = AS_STRING(bodyVal)->chars;
    body_len = AS_STRING(bodyVal)->length;
  }
  bool keep = IS_INT(keepVal) ? (AS_INT(keepVal) != 0) : false;
  const char *status_text = aot_http_status_text_cstr(status);
  size_t status_text_len = strlen(status_text);

  ObjObject *headers = nullptr;
  if (IS_OBJECT(headersVal)) {
    headers = (ObjObject *)AS_OBJECT(headersVal);
  }
  size_t extra_len = 0;
  auto eq_ci = [](const char *a, const char *b) {
    while (*a && *b) {
      char ca = *a++, cb = *b++;
      if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
      if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
      if (ca != cb) return false;
    }
    return *a == 0 && *b == 0;
  };
  if (headers) {
    for (int i = 0; i < headers->count; i++) {
      ObjString *k = headers->keys[i];
      ObjString *v = (IS_STRING(headers->values[i]))
                         ? AS_STRING(headers->values[i]) : nullptr;
      if (!k || !v) continue;
      if (eq_ci(k->chars, "Content-Type") ||
          eq_ci(k->chars, "Content-Length") ||
          eq_ci(k->chars, "Connection")) continue;
      extra_len += k->length + 2 + v->length + 2;
    }
  }

  size_t cap = 80 + content_type_len + extra_len + body_len + 64;

  // Allocate ObjString header + payload buffer in ONE arena block.
  // Previously this ran a two-step:
  //   1. arena_alloc(cap+1)              -> scratch char buffer
  //   2. aot_allocate_string(scratch, n) -> ObjString + chars (copies scratch)
  // i.e. one arena alloc + one full memcpy of the response bytes,
  // every HTTP response. By writing directly into the ObjString's
  // co-located chars buffer we drop the second alloc and the trailing
  // memcpy entirely. The cap is a safe upper bound (matches the
  // previous scratch size), so over-allocation by a few bytes is the
  // worst case — same arena lifetime semantics either way.
  size_t total = sizeof(ObjString) + cap + 1;
  char *block = (char *)aot_arena_alloc(total);
  ObjString *str = (ObjString *)block;
  str->obj.type = OBJ_STRING;
  str->obj.arena_allocated = 1;
  str->obj.next = nullptr;
  str->obj.ref_count = 1;
  str->obj.is_moved = 0;
  str->chars = block + sizeof(ObjString);
  str->hash = 0;
  char *w = str->chars;

  // "HTTP/1.1 <status> <status_text>\r\nContent-Type: <ct>\r\nContent-Length: <n>\r\n"
  // Hand-rolled because the snprintf equivalent re-parses the format
  // string and re-walks each %s/%d on every call — measurably costly
  // on a hot HTTP path. memcpy + fast_u32_itoa get the same bytes out
  // without the conversion-specifier machinery.
  memcpy(w, "HTTP/1.1 ", 9); w += 9;
  w = fast_u32_itoa(w, (uint32_t)status);
  *w++ = ' ';
  memcpy(w, status_text, status_text_len); w += status_text_len;
  memcpy(w, "\r\nContent-Type: ", 16); w += 16;
  memcpy(w, content_type, content_type_len); w += content_type_len;
  memcpy(w, "\r\nContent-Length: ", 18); w += 18;
  w = fast_u32_itoa(w, (uint32_t)body_len);
  *w++ = '\r'; *w++ = '\n';

  if (headers) {
    for (int i = 0; i < headers->count; i++) {
      ObjString *k = headers->keys[i];
      VMValue vv = headers->values[i];
      if (!k || !IS_STRING(vv)) continue;
      ObjString *v = AS_STRING(vv);
      if (eq_ci(k->chars, "Content-Type") ||
          eq_ci(k->chars, "Content-Length") ||
          eq_ci(k->chars, "Connection")) continue;
      memcpy(w, k->chars, k->length); w += k->length;
      *w++ = ':'; *w++ = ' ';
      memcpy(w, v->chars, v->length); w += v->length;
      *w++ = '\r'; *w++ = '\n';
    }
  }

  if (keep) {
    memcpy(w, "Connection: keep-alive\r\n\r\n", 26); w += 26;
  } else {
    memcpy(w, "Connection: close\r\n\r\n", 21); w += 21;
  }
  if (body_len > 0) {
    memcpy(w, body, body_len); w += body_len;
  }
  *w = '\0';

  str->length = (int)(w - str->chars);
  str->capacity = str->length + 1;
  return VM_OBJ((Obj *)str);
}

// Create HTTP response string
VMValue aot_http_create_response(VMValue statusVal, VMValue contentTypeVal,
                                 VMValue bodyVal) {
  int status = 200;
  if (IS_INT(statusVal)) {
    status = (int)AS_INT(statusVal);
  }

  const char *content_type = "text/plain";
  if (IS_STRING(contentTypeVal)) {
    content_type = AS_STRING(contentTypeVal)->chars;
  }

  const char *body = "";
  int body_len = 0;
  if (IS_STRING(bodyVal)) {
    body = AS_STRING(bodyVal)->chars;
    body_len = AS_STRING(bodyVal)->length;
  }

  // Status text — see aot_http_status_text below for the full mapping.
  const char *status_text = aot_http_status_text_cstr(status);

  // Build response
  char header[512];
  int header_len = snprintf(header, sizeof(header),
                            "HTTP/1.1 %d %s\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %d\r\n"
                            "Connection: close\r\n"
                            "\r\n",
                            status, status_text, content_type, body_len);

  // Allocate full response
  int total_len = header_len + body_len;
  char *response = static_cast<char *>(aot_arena_alloc(total_len + 1));
  memcpy(response, header, header_len);
  memcpy(response + header_len, body, body_len);
  response[total_len] = '\0';

  return VM_OBJ((Obj *)aot_allocate_string(response, total_len));
}

// wings_build_response(result, default_headers, keep) -> wire-string
//
// One-stop native replacement for the lib/wings.tpr `_wings_build_response`
// helper. Previously each request went through this Tulpar code:
//
//   func _wings_build_response(json result, int keep) {
//       int status = _wings_status_or(result, 200);   // tulpar fn call
//       if (_wings_has_raw(result)) { ... }           // tulpar fn call
//       return http_create_response(status, "application/json",
//           toJson(result), _default_headers, keep);  // 2 allocs
//   }
//
// Three Tulpar function calls + two arena allocations (toJson string +
// response string) per request. This builtin folds the envelope check,
// JSON serialisation, and response framing into a single C call with
// one arena allocation for the final wire bytes (toJson writes into a
// scratch buffer that's then memcpy'd into the response; we'll inline
// that further in a follow-up if needed).
//
// Recognises the same response envelope as the Tulpar code:
//   * `_status`       — override 200 default
//   * `_raw`          — body string is `_raw` verbatim (skip toJson)
//   * `_content_type` — Content-Type when `_raw` is set
//                       (defaults to "text/plain; charset=utf-8")
// True for the Wings response-envelope meta keys. These are read to build the
// HTTP envelope (status, raw body, content-type) but must NOT leak into the
// serialised JSON body — otherwise `created(x)` returns {"...":..,"_status":201}
// and clients see framework internals.
static inline bool wings_is_meta_key(const char *k) {
  return k[0] == '_' &&
         (strcmp(k, "_status") == 0 || strcmp(k, "_stream") == 0 ||
          strcmp(k, "_raw") == 0 || strcmp(k, "_content_type") == 0 ||
          strcmp(k, "_headers") == 0);
}

// Return `v` with the Wings meta keys stripped, for JSON-body serialisation.
// Fast path: when the object carries no meta key (the common case — a plain
// `ok(data)` whose data has none) the original value is returned untouched, so
// no copy is made. Only when a meta key is present do we build a request-local
// shallow copy (arena-tracked, freed at the next arena_restore) without them.
static VMValue aot_wings_strip_meta(VMValue v) {
  if (!IS_OBJECT(v))
    return v;
  ObjObject *src = (ObjObject *)AS_OBJECT(v);
  bool has_meta = false;
  for (int i = 0; i < src->count; i++) {
    if (src->keys[i] && wings_is_meta_key(src->keys[i]->chars)) {
      has_meta = true;
      break;
    }
  }
  if (!has_meta)
    return v;
  ObjObject *dst = vm_allocate_object_aot_wrapper(nullptr);
  for (int i = 0; i < src->count; i++) {
    if (!src->keys[i] || wings_is_meta_key(src->keys[i]->chars))
      continue;
    vm_object_set(nullptr, dst, src->keys[i]->chars, src->values[i]);
  }
  return VM_OBJ((Obj *)dst);
}

VMValue aot_wings_build_response(VMValue resultVal, VMValue defaultHeadersVal,
                                 VMValue keepVal) {
  int status = 200;
  const char *content_type_str = nullptr;
  ObjString *raw_body = nullptr;
  // Per-response extra headers (e.g. Location for redirect(), Set-Cookie).
  // Threaded into the keep-alive builder below, merged over the defaults.
  ObjObject *extra_headers = nullptr;

  if (IS_OBJECT(resultVal)) {
    ObjObject *obj = (ObjObject *)AS_OBJECT(resultVal);
    for (int i = 0; i < obj->count; i++) {
      ObjString *k = obj->keys[i];
      if (!k) continue;
      const char *key = k->chars;
      VMValue v = obj->values[i];
      if (strcmp(key, "_status") == 0 && IS_INT(v)) {
        status = (int)AS_INT(v);
      } else if (strcmp(key, "_raw") == 0 && IS_STRING(v)) {
        raw_body = AS_STRING(v);
      } else if (strcmp(key, "_content_type") == 0 && IS_STRING(v)) {
        content_type_str = AS_STRING(v)->chars;
      } else if (strcmp(key, "_headers") == 0 && IS_OBJECT(v)) {
        extra_headers = (ObjObject *)AS_OBJECT(v);
      }
    }
  }

  // Pick body + content type. Mirrors the Tulpar branch exactly so a
  // handler returning `{"_raw": "...", "_content_type": ""}` falls
  // back to text/plain just like before.
  VMValue body_val;
  if (raw_body) {
    if (!content_type_str || content_type_str[0] == '\0') {
      content_type_str = "text/plain; charset=utf-8";
    }
    body_val = VM_OBJ((Obj *)raw_body);
  } else {
    // Normal handler path: serialise the whole result to JSON. We
    // reuse aot_to_json (JSBuilder fast-path) rather than cJSON.
    content_type_str = "application/json";
    body_val = aot_to_json(aot_wings_strip_meta(resultVal));
  }

  // Stash content_type in a VMValue string so we can delegate the
  // actual wire framing to the existing keep-alive response builder.
  // This is the one allocation we don't fully fold today — collapsing
  // it would require teaching aot_http_create_response_keepalive to
  // take a (const char*, size_t) pair directly, which means a second
  // entry point. Left for a follow-up if profiling shows it matters.
  size_t ct_len = strlen(content_type_str);
  ObjString *ct_str = aot_allocate_string(content_type_str, (int)ct_len);
  VMValue ct_val = VM_OBJ((Obj *)ct_str);

  // When the handler supplied `_headers`, merge them over the default
  // headers so the keep-alive builder (which emits each header line and
  // already skips the Content-Type/Length/Connection it owns) writes
  // them onto the wire. Defaults first, per-response keys overlay.
  VMValue headers_val = defaultHeadersVal;
  if (extra_headers && extra_headers->count > 0) {
    ObjObject *merged = vm_allocate_object_aot_wrapper(nullptr);
    if (IS_OBJECT(defaultHeadersVal)) {
      ObjObject *dh = (ObjObject *)AS_OBJECT(defaultHeadersVal);
      for (int i = 0; i < dh->count; i++) {
        if (dh->keys[i])
          vm_object_set(nullptr, merged, dh->keys[i]->chars, dh->values[i]);
      }
    }
    for (int i = 0; i < extra_headers->count; i++) {
      if (extra_headers->keys[i])
        vm_object_set(nullptr, merged, extra_headers->keys[i]->chars,
                      extra_headers->values[i]);
    }
    headers_val = VM_OBJ((Obj *)merged);
  }

  return aot_http_create_response_keepalive(VM_INT(status), ct_val, body_val,
                                            headers_val, keepVal);
}

// ============================================================================
// Math Functions (AOT) - Using libm
// ============================================================================

// Close extern "C" block before including cmath (C++ header)
#ifdef __cplusplus
}
#endif

#include <cmath>

#ifdef __cplusplus
extern "C" {
#endif

void aot_math_abs(VMValue *result, VMValue *v_ptr) {
  VMValue v = *v_ptr;
  // fprintf(stderr, "DEBUG: abs input type=%d val=%lld\n", v.type,
  // v.as.int_val);
  if (IS_INT(v)) {
    int64_t val = AS_INT(v);
    // fprintf(stderr, "DEBUG: abs input int=%lld\n", val);
    *result = VM_INT(llabs(val));
    return;
  }
  if (IS_FLOAT(v)) {
    *result = VM_FLOAT(fabs(AS_FLOAT(v)));
    return;
  }
  *result = VM_INT(0);
}

VMValue aot_math_sqrt(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(sqrt(val));
}

VMValue aot_math_pow(VMValue base, VMValue exp) {
  double b = IS_INT(base) ? (double)AS_INT(base) : AS_FLOAT(base);
  double e = IS_INT(exp) ? (double)AS_INT(exp) : AS_FLOAT(exp);
  return VM_FLOAT(pow(b, e));
}

VMValue aot_math_floor(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(floor(val));
}

VMValue aot_math_ceil(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(ceil(val));
}

VMValue aot_math_round(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(round(val));
}

VMValue aot_math_sin(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(sin(val));
}

VMValue aot_math_cos(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(cos(val));
}

VMValue aot_math_tan(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(tan(val));
}

VMValue aot_math_asin(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(asin(val));
}

VMValue aot_math_acos(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(acos(val));
}

VMValue aot_math_atan(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(atan(val));
}

VMValue aot_math_atan2(VMValue y, VMValue x) {
  double yv = IS_INT(y) ? (double)AS_INT(y) : AS_FLOAT(y);
  double xv = IS_INT(x) ? (double)AS_INT(x) : AS_FLOAT(x);
  return VM_FLOAT(atan2(yv, xv));
}

VMValue aot_math_exp(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(exp(val));
}

VMValue aot_math_log(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(log(val));
}

VMValue aot_math_log10(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(log10(val));
}

VMValue aot_math_log2(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(log2(val));
}

VMValue aot_math_sinh(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(sinh(val));
}

VMValue aot_math_cosh(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(cosh(val));
}

VMValue aot_math_tanh(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(tanh(val));
}

VMValue aot_math_cbrt(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(cbrt(val));
}

VMValue aot_math_hypot(VMValue x, VMValue y) {
  double xv = IS_INT(x) ? (double)AS_INT(x) : AS_FLOAT(x);
  double yv = IS_INT(y) ? (double)AS_INT(y) : AS_FLOAT(y);
  return VM_FLOAT(hypot(xv, yv));
}

VMValue aot_math_trunc(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(trunc(val));
}

VMValue aot_math_fmod(VMValue x, VMValue y) {
  double xv = IS_INT(x) ? (double)AS_INT(x) : AS_FLOAT(x);
  double yv = IS_INT(y) ? (double)AS_INT(y) : AS_FLOAT(y);
  return VM_FLOAT(fmod(xv, yv));
}

VMValue aot_math_sqrt_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_sqrt(*v_ptr);
}

VMValue aot_math_pow_ptr(VMValue *b_ptr, VMValue *e_ptr) {
  if (!b_ptr || !e_ptr)
    return VM_INT(0);
  return aot_math_pow(*b_ptr, *e_ptr);
}

VMValue aot_math_floor_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_floor(*v_ptr);
}

VMValue aot_math_ceil_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_ceil(*v_ptr);
}

VMValue aot_math_round_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_round(*v_ptr);
}

VMValue aot_math_sin_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_sin(*v_ptr);
}

VMValue aot_math_cos_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_cos(*v_ptr);
}

VMValue aot_math_tan_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_tan(*v_ptr);
}

VMValue aot_math_asin_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_asin(*v_ptr);
}

VMValue aot_math_acos_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_acos(*v_ptr);
}

VMValue aot_math_atan_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_atan(*v_ptr);
}

VMValue aot_math_atan2_ptr(VMValue *y_ptr, VMValue *x_ptr) {
  if (!y_ptr || !x_ptr)
    return VM_INT(0);
  return aot_math_atan2(*y_ptr, *x_ptr);
}

VMValue aot_math_exp_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_exp(*v_ptr);
}

VMValue aot_math_log_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_log(*v_ptr);
}

VMValue aot_math_log10_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_log10(*v_ptr);
}

VMValue aot_math_log2_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_log2(*v_ptr);
}

VMValue aot_math_sinh_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_sinh(*v_ptr);
}

VMValue aot_math_cosh_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_cosh(*v_ptr);
}

VMValue aot_math_tanh_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_tanh(*v_ptr);
}

VMValue aot_math_cbrt_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_cbrt(*v_ptr);
}

VMValue aot_math_hypot_ptr(VMValue *x_ptr, VMValue *y_ptr) {
  if (!x_ptr || !y_ptr)
    return VM_INT(0);
  return aot_math_hypot(*x_ptr, *y_ptr);
}

VMValue aot_math_trunc_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_math_trunc(*v_ptr);
}

VMValue aot_math_fmod_ptr(VMValue *x_ptr, VMValue *y_ptr) {
  if (!x_ptr || !y_ptr)
    return VM_INT(0);
  return aot_math_fmod(*x_ptr, *y_ptr);
}

// Integer-flavoured modulo that mirrors the interpreter's `mod()` builtin:
// when both operands are int, return int (truncated `%`). Otherwise fall
// back to fmod for float-domain math. Modulo-by-zero returns 0 (quiet) to
// avoid crashing AOT'd binaries — divisor==0 is undefined for `%`.
VMValue aot_math_mod(VMValue a, VMValue b) {
  if (IS_INT(a) && IS_INT(b)) {
    int64_t bv = AS_INT(b);
    if (bv == 0)
      return VM_INT(0);
    return VM_INT(AS_INT(a) % bv);
  }
  double av = IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a);
  double bv = IS_INT(b) ? (double)AS_INT(b) : AS_FLOAT(b);
  if (bv == 0.0)
    return VM_FLOAT(0.0);
  return VM_FLOAT(fmod(av, bv));
}

VMValue aot_math_mod_ptr(VMValue *a_ptr, VMValue *b_ptr) {
  if (!a_ptr || !b_ptr)
    return VM_INT(0);
  return aot_math_mod(*a_ptr, *b_ptr);
}

// Random number generator (seeded on first call)
static int aot_random_seeded = 0;
VMValue aot_math_random(void) {
  if (!aot_random_seeded) {
    srand((unsigned int)time(nullptr));
    aot_random_seeded = 1;
  }
  return VM_FLOAT((double)rand() / RAND_MAX);
}

VMValue aot_math_randint(VMValue minVal, VMValue maxVal) {
  if (!aot_random_seeded) {
    srand((unsigned int)time(nullptr));
    aot_random_seeded = 1;
  }
  int64_t min = IS_INT(minVal) ? AS_INT(minVal) : (int64_t)AS_FLOAT(minVal);
  int64_t max = IS_INT(maxVal) ? AS_INT(maxVal) : (int64_t)AS_FLOAT(maxVal);
  return VM_INT(min + rand() % (max - min + 1));
}

VMValue aot_math_randint_ptr(VMValue *min_ptr, VMValue *max_ptr) {
  if (!min_ptr || !max_ptr)
    return VM_INT(0);
  return aot_math_randint(*min_ptr, *max_ptr);
}

VMValue aot_math_min(VMValue a, VMValue b) {
  double av = IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a);
  double bv = IS_INT(b) ? (double)AS_INT(b) : AS_FLOAT(b);
  return VM_FLOAT(av < bv ? av : bv);
}

VMValue aot_math_min_ptr(VMValue *a_ptr, VMValue *b_ptr) {
  if (!a_ptr || !b_ptr)
    return VM_INT(0);
  return aot_math_min(*a_ptr, *b_ptr);
}

VMValue aot_math_max(VMValue a, VMValue b) {
  double av = IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a);
  double bv = IS_INT(b) ? (double)AS_INT(b) : AS_FLOAT(b);
  return VM_FLOAT(av > bv ? av : bv);
}

VMValue aot_math_max_ptr(VMValue *a_ptr, VMValue *b_ptr) {
  if (!a_ptr || !b_ptr)
    return VM_INT(0);
  return aot_math_max(*a_ptr, *b_ptr);
}

// ============================================================================
// String Functions (AOT) - Extended
// ============================================================================

VMValue aot_string_upper(VMValue v) {
  if (!IS_STRING(v))
    return v;
  ObjString *s = AS_STRING(v);
  char *result = static_cast<char *>(aot_arena_alloc(s->length + 1));
  for (int i = 0; i < s->length; i++) {
    result[i] = toupper((unsigned char)s->chars[i]);
  }
  result[s->length] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, s->length));
}

VMValue aot_string_upper_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_string_upper(*v_ptr);
}

VMValue aot_string_lower(VMValue v) {
  if (!IS_STRING(v))
    return v;
  ObjString *s = AS_STRING(v);
  char *result = static_cast<char *>(aot_arena_alloc(s->length + 1));
  for (int i = 0; i < s->length; i++) {
    result[i] = tolower((unsigned char)s->chars[i]);
  }
  result[s->length] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, s->length));
}

VMValue aot_string_lower_ptr(VMValue *v_ptr) {
  if (!v_ptr)
    return VM_INT(0);
  return aot_string_lower(*v_ptr);
}

VMValue aot_string_contains(VMValue haystack, VMValue needle) {
  if (!IS_STRING(haystack) || !IS_STRING(needle))
    return VM_BOOL(0);
  ObjString *h = AS_STRING(haystack);
  ObjString *n = AS_STRING(needle);
  return VM_BOOL(strstr(h->chars, n->chars) != nullptr);
}

VMValue aot_string_contains_ptr(VMValue *h_ptr, VMValue *n_ptr) {
  if (!h_ptr || !n_ptr)
    return VM_BOOL(0);
  return aot_string_contains(*h_ptr, *n_ptr);
}

VMValue aot_string_starts_with(VMValue str, VMValue prefix) {
  if (!IS_STRING(str) || !IS_STRING(prefix))
    return VM_BOOL(0);
  ObjString *s = AS_STRING(str);
  ObjString *p = AS_STRING(prefix);
  if (p->length > s->length)
    return VM_BOOL(0);
  return VM_BOOL(strncmp(s->chars, p->chars, p->length) == 0);
}

VMValue aot_string_starts_with_ptr(VMValue *s_ptr, VMValue *p_ptr) {
  if (!s_ptr || !p_ptr)
    return VM_BOOL(0);
  return aot_string_starts_with(*s_ptr, *p_ptr);
}

VMValue aot_string_ends_with(VMValue str, VMValue suffix) {
  if (!IS_STRING(str) || !IS_STRING(suffix))
    return VM_BOOL(0);
  ObjString *s = AS_STRING(str);
  ObjString *x = AS_STRING(suffix);
  if (x->length > s->length)
    return VM_BOOL(0);
  return VM_BOOL(strcmp(s->chars + s->length - x->length, x->chars) == 0);
}

VMValue aot_string_ends_with_ptr(VMValue *s_ptr, VMValue *x_ptr) {
  if (!s_ptr || !x_ptr)
    return VM_BOOL(0);
  return aot_string_ends_with(*s_ptr, *x_ptr);
}

VMValue aot_string_index_of(VMValue haystack, VMValue needle) {
  if (!IS_STRING(haystack) || !IS_STRING(needle))
    return VM_INT(-1);
  ObjString *h = AS_STRING(haystack);
  ObjString *n = AS_STRING(needle);
  const char *pos = strstr(h->chars, n->chars);
  if (pos == nullptr)
    return VM_INT(-1);
  return VM_INT(pos - h->chars);
}

VMValue aot_string_index_of_ptr(VMValue *h_ptr, VMValue *n_ptr) {
  if (!h_ptr || !n_ptr)
    return VM_INT(-1);
  return aot_string_index_of(*h_ptr, *n_ptr);
}

VMValue aot_string_substring(VMValue str, VMValue startVal, VMValue endVal) {
  if (!IS_STRING(str))
    return str;
  ObjString *s = AS_STRING(str);
  int start = IS_INT(startVal) ? (int)AS_INT(startVal) : 0;
  int end = IS_INT(endVal) ? (int)AS_INT(endVal) : s->length;

  if (start < 0)
    start = 0;
  if (end > s->length)
    end = s->length;
  if (start >= end)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  int len = end - start;
  return VM_OBJ((Obj *)aot_allocate_string(s->chars + start, len));
}

VMValue aot_string_substring_ptr(VMValue *s_ptr, VMValue *start_ptr,
                                 VMValue *end_ptr) {
  if (!s_ptr || !start_ptr || !end_ptr)
    return VM_INT(0);
  return aot_string_substring(*s_ptr, *start_ptr, *end_ptr);
}

VMValue aot_string_repeat(VMValue str, VMValue countVal) {
  if (!IS_STRING(str) || !IS_INT(countVal))
    return str;
  ObjString *s = AS_STRING(str);
  int count = (int)AS_INT(countVal);
  if (count <= 0)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  int total_len = s->length * count;
  char *result = static_cast<char *>(aot_arena_alloc(total_len + 1));
  for (int i = 0; i < count; i++) {
    memcpy(result + i * s->length, s->chars, s->length);
  }
  result[total_len] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, total_len));
}

VMValue aot_string_repeat_ptr(VMValue *s_ptr, VMValue *c_ptr) {
  if (!s_ptr || !c_ptr)
    return VM_INT(0);
  return aot_string_repeat(*s_ptr, *c_ptr);
}

VMValue aot_string_reverse(VMValue str) {
  if (!IS_STRING(str))
    return str;
  ObjString *s = AS_STRING(str);
  char *result = static_cast<char *>(aot_arena_alloc(s->length + 1));
  for (int i = 0; i < s->length; i++) {
    result[i] = s->chars[s->length - 1 - i];
  }
  result[s->length] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, s->length));
}

VMValue aot_string_reverse_ptr(VMValue *s_ptr) {
  if (!s_ptr)
    return VM_INT(0);
  return aot_string_reverse(*s_ptr);
}

VMValue aot_string_is_empty(VMValue str) {
  if (!IS_STRING(str))
    return VM_BOOL(1);
  return VM_BOOL(AS_STRING(str)->length == 0);
}

VMValue aot_string_is_empty_ptr(VMValue *s_ptr) {
  if (!s_ptr)
    return VM_BOOL(1);
  return aot_string_is_empty(*s_ptr);
}

VMValue aot_string_count(VMValue haystack, VMValue needle) {
  if (!IS_STRING(haystack) || !IS_STRING(needle))
    return VM_INT(0);
  ObjString *h = AS_STRING(haystack);
  ObjString *n = AS_STRING(needle);
  if (n->length == 0)
    return VM_INT(0);

  int count = 0;
  const char *pos = h->chars;
  while ((pos = strstr(pos, n->chars)) != nullptr) {
    count++;
    pos += n->length;
  }
  return VM_INT(count);
}

VMValue aot_string_count_ptr(VMValue *h_ptr, VMValue *n_ptr) {
  if (!h_ptr || !n_ptr)
    return VM_INT(0);
  return aot_string_count(*h_ptr, *n_ptr);
}

VMValue aot_string_capitalize(VMValue str) {
  if (!IS_STRING(str))
    return str;
  ObjString *s = AS_STRING(str);
  if (s->length == 0)
    return str;

  char *result = static_cast<char *>(aot_arena_alloc(s->length + 1));
  result[0] = toupper((unsigned char)s->chars[0]);
  for (int i = 1; i < s->length; i++) {
    result[i] = tolower((unsigned char)s->chars[i]);
  }
  result[s->length] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, s->length));
}

VMValue aot_string_capitalize_ptr(VMValue *s_ptr) {
  if (!s_ptr)
    return VM_INT(0);
  return aot_string_capitalize(*s_ptr);
}

VMValue aot_string_is_digit(VMValue str) {
  if (!IS_STRING(str))
    return VM_BOOL(0);
  ObjString *s = AS_STRING(str);
  if (s->length == 0)
    return VM_BOOL(0);
  for (int i = 0; i < s->length; i++) {
    if (!isdigit((unsigned char)s->chars[i]))
      return VM_BOOL(0);
  }
  return VM_BOOL(1);
}

VMValue aot_string_is_digit_ptr(VMValue *s_ptr) {
  if (!s_ptr)
    return VM_BOOL(0);
  return aot_string_is_digit(*s_ptr);
}

VMValue aot_string_is_alpha(VMValue str) {
  if (!IS_STRING(str))
    return VM_BOOL(0);
  ObjString *s = AS_STRING(str);
  if (s->length == 0)
    return VM_BOOL(0);
  for (int i = 0; i < s->length; i++) {
    if (!isalpha((unsigned char)s->chars[i]))
      return VM_BOOL(0);
  }
  return VM_BOOL(1);
}

VMValue aot_string_is_alpha_ptr(VMValue *s_ptr) {
  if (!s_ptr)
    return VM_BOOL(0);
  return aot_string_is_alpha(*s_ptr);
}

VMValue aot_string_join(VMValue sep, VMValue arr) {
  if (!IS_STRING(sep) || !IS_ARRAY(arr))
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  ObjString *separator = AS_STRING(sep);
  ObjArray *array = AS_ARRAY(arr);

  if (array->count == 0)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  // Calculate total length
  int total_len = 0;
  for (int i = 0; i < array->count; i++) {
    if (IS_STRING(array->items[i])) {
      total_len += AS_STRING(array->items[i])->length;
    }
    if (i > 0)
      total_len += separator->length;
  }

  char *result = static_cast<char *>(aot_arena_alloc(total_len + 1));
  int pos = 0;
  for (int i = 0; i < array->count; i++) {
    if (i > 0) {
      memcpy(result + pos, separator->chars, separator->length);
      pos += separator->length;
    }
    if (IS_STRING(array->items[i])) {
      ObjString *s = AS_STRING(array->items[i]);
      memcpy(result + pos, s->chars, s->length);
      pos += s->length;
    }
  }
  result[total_len] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, total_len));
}

VMValue aot_string_join_ptr(VMValue *sep_ptr, VMValue *arr_ptr) {
  if (!sep_ptr || !arr_ptr)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  return aot_string_join(*sep_ptr, *arr_ptr);
}

// ============================================================================
// Time Functions (AOT) - Extended
// ============================================================================

VMValue aot_timestamp(void) { return VM_INT((int64_t)time(nullptr)); }

VMValue aot_time_ms(void) {
  // Delegate to the shared cross-platform helper in common/platform.h so the
  // Win/POSIX branches live in one place.
  return VM_INT((int64_t)tulpar_epoch_ms());
}

void aot_sleep(VMValue msVal) {
  if (!IS_INT(msVal))
    return;
  int64_t ms = AS_INT(msVal);
  tulpar_sleep_ms((unsigned int)ms);
}

void aot_sleep_ptr(VMValue *ms_ptr) {
  if (!ms_ptr)
    return;
  aot_sleep(*ms_ptr);
}

// ============================================================================
// JSON Deserialization (AOT) - fromJson
// ============================================================================

// Forward declaration for recursive parsing
static VMValue parse_json_value(const char **p, const char *end);

static void skip_whitespace(const char **p, const char *end) {
  while (*p < end && isspace(**p))
    (*p)++;
}

static VMValue parse_json_string(const char **p, const char *end) {
  if (**p != '"')
    return VM_INT(0);
  (*p)++; // skip opening quote

  const char *start = *p;
  int len = 0;

  // Find end of string (handle escapes later)
  while (*p < end && **p != '"') {
    if (**p == '\\' && *p + 1 < end) {
      (*p) += 2; // skip escape sequence
      len += 1;  // escaped char counts as 1
    } else {
      (*p)++;
      len++;
    }
  }

  // Build unescaped string
  char *result = static_cast<char *>(aot_arena_alloc(len + 1));
  const char *src = start;
  int i = 0;
  while (src < *p) {
    if (*src == '\\' && src + 1 < *p) {
      src++;
      switch (*src) {
      case 'n':
        result[i++] = '\n';
        break;
      case 't':
        result[i++] = '\t';
        break;
      case 'r':
        result[i++] = '\r';
        break;
      case '"':
        result[i++] = '"';
        break;
      case '\\':
        result[i++] = '\\';
        break;
      default:
        result[i++] = *src;
        break;
      }
      src++;
    } else {
      result[i++] = *src++;
    }
  }
  result[i] = '\0';

  if (*p < end)
    (*p)++; // skip closing quote

  return VM_OBJ((Obj *)aot_allocate_string(result, i));
}

static VMValue parse_json_number(const char **p, const char *end) {
  const char *start = *p;
  int is_float = 0;

  if (**p == '-')
    (*p)++;
  while (*p < end && isdigit(**p))
    (*p)++;

  if (*p < end && **p == '.') {
    is_float = 1;
    (*p)++;
    while (*p < end && isdigit(**p))
      (*p)++;
  }

  if (*p < end && (**p == 'e' || **p == 'E')) {
    is_float = 1;
    (*p)++;
    if (*p < end && (**p == '+' || **p == '-'))
      (*p)++;
    while (*p < end && isdigit(**p))
      (*p)++;
  }

  char buf[64];
  int len = *p - start;
  if (len >= 64)
    len = 63;
  strncpy(buf, start, len);
  buf[len] = '\0';

  if (is_float) {
    return VM_FLOAT(atof(buf));
  }
  return VM_INT(atoll(buf));
}

static VMValue parse_json_array(const char **p, const char *end) {
  if (**p != '[')
    return VM_INT(0);
  (*p)++; // skip [

  ObjArray *arr = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
  arr->obj.type = OBJ_ARRAY;
  arr->obj.arena_allocated = 1;
  arr->obj.next = nullptr;
  arr->capacity = 8;
  arr->count = 0;
  arr->items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * arr->capacity);

  skip_whitespace(p, end);

  while (*p < end && **p != ']') {
    VMValue val = parse_json_value(p, end);

    // Grow array if needed
    if (arr->count >= arr->capacity) {
      int new_cap = arr->capacity * 2;
      VMValue *new_items =
          (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
      memcpy(new_items, arr->items, sizeof(VMValue) * arr->count);
      arr->items = new_items;
      arr->capacity = new_cap;
    }
    arr->items[arr->count++] = val;

    skip_whitespace(p, end);
    if (*p < end && **p == ',') {
      (*p)++;
      skip_whitespace(p, end);
    }
  }

  if (*p < end)
    (*p)++; // skip ]

  return VM_OBJ((Obj *)arr);
}

static VMValue parse_json_object(const char **p, const char *end) {
  if (**p != '{')
    return VM_INT(0);
  (*p)++; // skip {

  ObjObject *obj = (ObjObject *)aot_arena_alloc(sizeof(ObjObject));
  obj->obj.type = OBJ_OBJECT;
  obj->obj.arena_allocated = 1;
  obj->obj.next = nullptr;
  obj->capacity = 8;
  obj->count = 0;
  obj->keys =
      (ObjString **)aot_arena_alloc(sizeof(ObjString *) * obj->capacity);
  obj->values = (VMValue *)aot_arena_alloc(sizeof(VMValue) * obj->capacity);

  skip_whitespace(p, end);

  while (*p < end && **p != '}') {
    // Parse key
    VMValue key_val = parse_json_string(p, end);
    if (!IS_STRING(key_val))
      break;

    skip_whitespace(p, end);
    if (*p < end && **p == ':')
      (*p)++;
    skip_whitespace(p, end);

    // Parse value
    VMValue val = parse_json_value(p, end);

    // Grow object if needed
    if (obj->count >= obj->capacity) {
      int new_cap = obj->capacity * 2;
      ObjString **new_keys =
          (ObjString **)aot_arena_alloc(sizeof(ObjString *) * new_cap);
      VMValue *new_vals = (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
      memcpy(new_keys, obj->keys, sizeof(ObjString *) * obj->count);
      memcpy(new_vals, obj->values, sizeof(VMValue) * obj->count);
      obj->keys = new_keys;
      obj->values = new_vals;
      obj->capacity = new_cap;
    }

    obj->keys[obj->count] = AS_STRING(key_val);
    obj->values[obj->count] = val;
    obj->count++;

    skip_whitespace(p, end);
    if (*p < end && **p == ',') {
      (*p)++;
      skip_whitespace(p, end);
    }
  }

  if (*p < end)
    (*p)++; // skip }

  return VM_OBJ((Obj *)obj);
}

static VMValue parse_json_value(const char **p, const char *end) {
  skip_whitespace(p, end);
  if (*p >= end)
    return VM_INT(0);

  char c = **p;

  if (c == '"') {
    return parse_json_string(p, end);
  } else if (c == '[') {
    return parse_json_array(p, end);
  } else if (c == '{') {
    return parse_json_object(p, end);
  } else if (c == 't' && strncmp(*p, "true", 4) == 0) {
    *p += 4;
    return VM_BOOL(1);
  } else if (c == 'f' && strncmp(*p, "false", 5) == 0) {
    *p += 5;
    return VM_BOOL(0);
  } else if (c == 'n' && strncmp(*p, "nullptr", 4) == 0) {
    *p += 4;
    return VM_INT(0);
  } else if (c == '-' || isdigit(c)) {
    return parse_json_number(p, end);
  }

  return VM_INT(0);
}

VMValue aot_from_json(VMValue jsonStr) {
  if (!IS_STRING(jsonStr))
    return VM_INT(0);

  ObjString *s = AS_STRING(jsonStr);
  const char *p = s->chars;
  const char *end = s->chars + s->length;

  return parse_json_value(&p, end);
}

// ============================================================================
// Input Functions (AOT) - Extended
// ============================================================================

VMValue aot_input_int(VMValue promptVal) {
  if (IS_STRING(promptVal)) {
    printf("%s", AS_STRING(promptVal)->chars);
    fflush(stdout);
  }

  char buf[64];
  if (fgets(buf, sizeof(buf), stdin)) {
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
      buf[len - 1] = '\0';
      len--;
    }
    if (len > 0 && buf[len - 1] == '\r') {
      buf[len - 1] = '\0';
    }
    return VM_INT(atoll(buf));
  }
  return VM_INT(0);
}

VMValue aot_input_float(VMValue promptVal) {
  if (IS_STRING(promptVal)) {
    printf("%s", AS_STRING(promptVal)->chars);
    fflush(stdout);
  }

  char buf[64];
  if (fgets(buf, sizeof(buf), stdin)) {
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
      buf[len - 1] = '\0';
      len--;
    }
    if (len > 0 && buf[len - 1] == '\r') {
      buf[len - 1] = '\0';
    }
    return VM_FLOAT(atof(buf));
  }
  return VM_FLOAT(0.0);
}

// ============================================================================
// Range Function (AOT)
// ============================================================================

VMValue aot_range(VMValue endVal) {
  int64_t end = IS_INT(endVal) ? AS_INT(endVal) : 0;
  if (end <= 0) {
    ObjArray *arr = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
    arr->obj.type = OBJ_ARRAY;
    arr->obj.arena_allocated = 1;
    arr->obj.next = nullptr;
    arr->capacity = 0;
    arr->count = 0;
    arr->items = nullptr;
    return VM_OBJ((Obj *)arr);
  }

  ObjArray *arr = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
  arr->obj.type = OBJ_ARRAY;
  arr->obj.arena_allocated = 1;
  arr->obj.next = nullptr;
  arr->capacity = (int)end;
  arr->count = (int)end;
  arr->items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * end);

  for (int64_t i = 0; i < end; i++) {
    arr->items[i] = VM_INT(i);
  }

  return VM_OBJ((Obj *)arr);
}

// ============================================================================
// SQLite Database Functions (AOT)
// ============================================================================
#include "../../lib/sqlite3/sqlite3.h"

// ----------------------------------------------------------------------------
// Per-thread connection registry (parallel reads under WAL)
//
// A db handle is no longer a raw sqlite3* but a 1-based index into a registry
// of DbConn descriptors. Each worker thread lazily opens its OWN sqlite3
// connection to the same file, so under WAL many threads read in parallel
// instead of serializing on one shared connection's internal mutex (the
// bottleneck the wings stress test exposed: pool reads didn't scale with
// workers). :memory:/temp DBs can't be shared across independent connections
// — each open() would see a fresh empty DB — so they keep a single shared
// connection (the historical serialized behavior).
// ----------------------------------------------------------------------------
struct DbConn {
  std::string path;
  bool wal = false;             // apply WAL pragmas on each connection
  bool shared = false;          // :memory:/temp -> one shared connection
  sqlite3 *shared_conn = nullptr;
  std::mutex conns_mu;          // guards conns (file-backed mode)
  std::vector<sqlite3 *> conns; // every per-thread conn opened, for db_close
  std::atomic<bool> closed{false};
};

static std::mutex g_db_registry_mu;
static std::vector<DbConn *> g_db_registry; // index = id (handle - 1)

// Per-thread cache: descriptor id (0-based) -> this thread's connection.
static thread_local std::vector<sqlite3 *> g_db_tls_conns;

// Per-thread prepared-statement cache. Keyed by connection pointer — since
// connections are per-thread (g_db_tls_conns), this is naturally lock-free on
// the hot path. db_query reuses a compiled statement via sqlite3_reset instead
// of re-preparing identical SQL, a win for read-heavy endpoints running the
// same SELECT repeatedly. Queries are raw SQL strings (no bound params), so
// only EXACT repeats hit; values baked into the SQL each get their own entry.
// Bounded with FIFO eviction (the evicted statement is finalized). prepare_v2
// auto-reprepares cached statements across schema changes, so caching is safe.
// db_close uses sqlite3_close_v2 so any still-cached statements don't block the
// close (the connection is reclaimed once they finalize, e.g. on eviction).
struct StmtCacheEntry {
  std::string sql;
  sqlite3_stmt *stmt;
};
static const size_t kStmtCacheCap = 64;
static thread_local std::unordered_map<sqlite3 *, std::vector<StmtCacheEntry>>
    g_stmt_cache;

// Return a ready-to-step statement for `sql` on `db`: a reset cache hit, or a
// freshly prepared + cached statement. nullptr on prepare failure. The caller
// must NOT finalize the result — it stays cached. Call sqlite3_reset after use
// to release locks (the next reuse resets again, which is harmless).
static sqlite3_stmt *db_cached_prepare(sqlite3 *db, const char *sql,
                                       int sql_len) {
  std::vector<StmtCacheEntry> &cache = g_stmt_cache[db];
  for (size_t i = 0; i < cache.size(); i++) {
    if (cache[i].sql.size() == (size_t)sql_len &&
        memcmp(cache[i].sql.data(), sql, sql_len) == 0) {
      sqlite3_reset(cache[i].stmt);
      return cache[i].stmt;
    }
  }
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, sql_len, &stmt, nullptr) != SQLITE_OK || !stmt)
    return nullptr;
  if (cache.size() >= kStmtCacheCap) {
    sqlite3_finalize(cache.front().stmt); // FIFO evict
    cache.erase(cache.begin());
  }
  cache.push_back({std::string(sql, (size_t)sql_len), stmt});
  return stmt;
}

// Finalize and drop THIS thread's cached statements for `db` (called on close
// so the common single-thread / shared path is fully leak-free).
static void db_drop_stmt_cache(sqlite3 *db) {
  std::unordered_map<sqlite3 *, std::vector<StmtCacheEntry>>::iterator it =
      g_stmt_cache.find(db);
  if (it == g_stmt_cache.end())
    return;
  for (size_t i = 0; i < it->second.size(); i++)
    sqlite3_finalize(it->second[i].stmt);
  g_stmt_cache.erase(it);
}

static void db_apply_pragmas(sqlite3 *db, bool wal) {
  // busy_timeout: on a locked DB, block-and-retry for up to 5s instead of
  // failing immediately with SQLITE_BUSY (matters now that independent
  // per-thread connections contend for the single writer slot).
  sqlite3_busy_timeout(db, 5000);
  // WAL + synchronous=NORMAL on file-backed DBs: ~2.3x write throughput, and
  // crucially WAL is what lets readers proceed concurrently with one writer.
  if (wal) {
    sqlite3_exec(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;",
                 nullptr, nullptr, nullptr);
  }
  // Per-connection page-cache cap. With the per-thread connection model the
  // page cache is paid once PER connection, so N worker threads multiply it
  // — an unbounded default would let RSS balloon under listen_pool. Negative
  // cache_size = KiB (positive would be pages, which varies with page_size).
  // Default 2 MiB; override with TULPAR_DB_CACHE_KB to tune the RSS/throughput
  // trade-off on many-connection servers.
  int cache_kb = 2048;
  const char *cache_env = getenv("TULPAR_DB_CACHE_KB");
  if (cache_env && *cache_env) {
    int v = atoi(cache_env);
    if (v > 0)
      cache_kb = v;
  }
  char cache_pragma[64];
  snprintf(cache_pragma, sizeof(cache_pragma), "PRAGMA cache_size=-%d;",
           cache_kb);
  sqlite3_exec(db, cache_pragma, nullptr, nullptr, nullptr);
}

// Open a fresh connection for `desc` and register it for db_close cleanup.
static sqlite3 *db_open_conn(DbConn *desc) {
  sqlite3 *db = nullptr;
  if (sqlite3_open(desc->path.c_str(), &db) != SQLITE_OK) {
    if (db)
      sqlite3_close(db);
    return nullptr;
  }
  db_apply_pragmas(db, desc->wal);
  {
    std::lock_guard<std::mutex> g(desc->conns_mu);
    desc->conns.push_back(db);
  }
  return db;
}

// Resolve a handle to the connection THIS thread should use. For file-backed
// DBs the connection is opened lazily on first use per thread.
static sqlite3 *db_resolve(VMValue dbVal) {
  if (!IS_INT(dbVal))
    return nullptr;
  int64_t h = AS_INT(dbVal);
  if (h <= 0)
    return nullptr;
  size_t id = (size_t)(h - 1);
  DbConn *desc = nullptr;
  {
    std::lock_guard<std::mutex> g(g_db_registry_mu);
    if (id >= g_db_registry.size())
      return nullptr;
    desc = g_db_registry[id];
  }
  // closed checked before consulting the tls cache, so a connection already
  // closed by db_close (possibly from another thread at shutdown) is never
  // reused through a stale per-thread pointer.
  if (!desc || desc->closed.load())
    return nullptr;
  if (desc->shared)
    return desc->shared_conn;
  if (g_db_tls_conns.size() <= id)
    g_db_tls_conns.resize(id + 1, nullptr);
  sqlite3 *db = g_db_tls_conns[id];
  if (!db) {
    db = db_open_conn(desc);
    g_db_tls_conns[id] = db;
  }
  return db;
}

// db_open(path) -> db_handle (int64, 1-based registry index)
VMValue aot_db_open(VMValue pathVal) {
  if (!IS_STRING(pathVal))
    return VM_INT(0);

  ObjString *path = AS_STRING(pathVal);
  const char *p = path->chars;
  bool file_backed = p && p[0] != '\0' && strcmp(p, ":memory:") != 0;
  const char *no_wal = getenv("TULPAR_DB_NO_WAL");
  // Opt out of WAL with TULPAR_DB_NO_WAL=1 (e.g. a DB on a network FS that
  // can't do WAL).
  bool wal = file_backed && (!no_wal || no_wal[0] == '\0');

  DbConn *desc = new DbConn();
  desc->path = p ? p : "";
  desc->wal = wal;
  desc->shared = !file_backed;

  sqlite3 *first_conn = nullptr;
  if (desc->shared) {
    // :memory:/temp — one connection shared by all threads (serialized; an
    // independent connection can't see another's in-memory DB).
    if (sqlite3_open(desc->path.c_str(), &first_conn) != SQLITE_OK) {
      if (first_conn)
        sqlite3_close(first_conn);
      delete desc;
      return VM_INT(0);
    }
    db_apply_pragmas(first_conn, false);
    desc->shared_conn = first_conn;
  } else {
    // file-backed — open the calling thread's connection now to validate the
    // path and put the file into WAL mode; other threads open lazily.
    first_conn = db_open_conn(desc);
    if (!first_conn) {
      delete desc;
      return VM_INT(0);
    }
  }

  size_t id;
  {
    std::lock_guard<std::mutex> g(g_db_registry_mu);
    id = g_db_registry.size();
    g_db_registry.push_back(desc);
  }
  if (!desc->shared) {
    // Cache the just-opened connection in this thread's slot.
    if (g_db_tls_conns.size() <= id)
      g_db_tls_conns.resize(id + 1, nullptr);
    g_db_tls_conns[id] = first_conn;
  }
  return VM_INT((int64_t)(id + 1));
}

VMValue aot_db_open_ptr(VMValue *path_ptr) {
  if (!path_ptr)
    return VM_INT(0);
  return aot_db_open(*path_ptr);
}

// db_close(db_handle) -> void
void aot_db_close(VMValue dbVal) {
  if (!IS_INT(dbVal))
    return;
  int64_t h = AS_INT(dbVal);
  if (h <= 0)
    return;
  size_t id = (size_t)(h - 1);
  DbConn *desc = nullptr;
  {
    std::lock_guard<std::mutex> g(g_db_registry_mu);
    if (id >= g_db_registry.size())
      return;
    desc = g_db_registry[id];
  }
  if (!desc)
    return;
  // Idempotent: only the first close does the work. db_close is a shutdown
  // operation — closing connections still cached by other worker threads is
  // safe only because no request is mid-flight at that point, and db_resolve
  // gates on `closed` so those threads stop using their cached pointers.
  if (desc->closed.exchange(true))
    return;
  // Finalize this thread's cached prepared statements for the connections being
  // closed (keeps the common single-thread / shared :memory: path leak-free),
  // then close. sqlite3_close_v2 tolerates statements still cached by OTHER
  // worker threads — the connection is reclaimed once those finalize (on
  // eviction) instead of failing with SQLITE_BUSY.
  if (desc->shared) {
    if (desc->shared_conn) {
      db_drop_stmt_cache(desc->shared_conn);
      sqlite3_close_v2(desc->shared_conn);
      desc->shared_conn = nullptr;
    }
  } else {
    std::lock_guard<std::mutex> g(desc->conns_mu);
    for (sqlite3 *db : desc->conns)
      if (db) {
        db_drop_stmt_cache(db);
        sqlite3_close_v2(db);
      }
    desc->conns.clear();
  }
  if (g_db_tls_conns.size() > id)
    g_db_tls_conns[id] = nullptr;
}

void aot_db_close_ptr(VMValue *db_ptr) {
  if (!db_ptr)
    return;
  aot_db_close(*db_ptr);
}

// db_execute(db_handle, sql) -> bool (success)
VMValue aot_db_execute(VMValue dbVal, VMValue sqlVal) {
  if (!IS_STRING(sqlVal))
    return VM_BOOL(0);

  sqlite3 *db = db_resolve(dbVal);
  ObjString *sql = AS_STRING(sqlVal);

  if (!db)
    return VM_BOOL(0);

  char *errMsg = nullptr;
  int rc = sqlite3_exec(db, sql->chars, nullptr, nullptr, &errMsg);

  if (errMsg) {
    sqlite3_free(errMsg);
  }

  return VM_BOOL(rc == SQLITE_OK);
}

VMValue aot_db_execute_ptr(VMValue *db_ptr, VMValue *sql_ptr) {
  if (!db_ptr || !sql_ptr)
    return VM_BOOL(0);
  return aot_db_execute(*db_ptr, *sql_ptr);
}

// db_query(db_handle, sql) -> array of objects (rows)
VMValue aot_db_query(VMValue dbVal, VMValue sqlVal) {
  if (!IS_STRING(sqlVal)) {
    // Return empty array
    ObjArray *arr = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
    arr->obj.type = OBJ_ARRAY;
    arr->obj.arena_allocated = 1;
    arr->obj.next = nullptr;
    arr->capacity = 0;
    arr->count = 0;
    arr->items = nullptr;
    return VM_OBJ((Obj *)arr);
  }

  sqlite3 *db = db_resolve(dbVal);
  ObjString *sql = AS_STRING(sqlVal);

  // Prepare result array
  ObjArray *result = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
  result->obj.type = OBJ_ARRAY;
  result->obj.arena_allocated = 1;
  result->obj.next = nullptr;
  result->capacity = 16;
  result->count = 0;
  result->items =
      (VMValue *)aot_arena_alloc(sizeof(VMValue) * result->capacity);

  if (!db)
    return VM_OBJ((Obj *)result);

  // Cached prepare: reuse a compiled statement for identical SQL on this
  // connection instead of prepare+finalize per call.
  sqlite3_stmt *stmt = db_cached_prepare(db, sql->chars, sql->length);

  if (!stmt) {
    if (getenv("TULPAR_DB_DEBUG"))
      fprintf(stderr, "[dbq] prepare failed (%s)\n", sqlite3_errmsg(db));
    return VM_OBJ((Obj *)result);
  }

  int col_count = sqlite3_column_count(stmt);

  // Fetch rows
  int step_rc;
  while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    // Create object for this row
    ObjObject *row = (ObjObject *)aot_arena_alloc(sizeof(ObjObject));
    row->obj.type = OBJ_OBJECT;
    row->obj.arena_allocated = 1;
    row->obj.next = nullptr;
    row->capacity = col_count;
    row->count = 0;
    row->keys = (ObjString **)aot_arena_alloc(sizeof(ObjString *) * col_count);
    row->values = (VMValue *)aot_arena_alloc(sizeof(VMValue) * col_count);

    for (int i = 0; i < col_count; i++) {
      const char *col_name = sqlite3_column_name(stmt, i);
      row->keys[row->count] = aot_allocate_string(col_name, strlen(col_name));

      // Get value based on type
      int col_type = sqlite3_column_type(stmt, i);
      VMValue val;

      switch (col_type) {
      case SQLITE_INTEGER:
        val = VM_INT(sqlite3_column_int64(stmt, i));
        break;
      case SQLITE_FLOAT:
        val = VM_FLOAT(sqlite3_column_double(stmt, i));
        break;
      case SQLITE_TEXT: {
        const char *text = (const char *)sqlite3_column_text(stmt, i);
        int len = sqlite3_column_bytes(stmt, i);
        val = VM_OBJ((Obj *)aot_allocate_string(text, len));
        break;
      }
      case SQLITE_BLOB:
      case SQLITE_NULL:
      default:
        val = VM_INT(0); // nullptr
        break;
      }

      row->values[row->count] = val;
      row->count++;
    }

    // Grow result array if needed
    if (result->count >= result->capacity) {
      int new_cap = result->capacity * 2;
      VMValue *new_items =
          (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
      memcpy(new_items, result->items, sizeof(VMValue) * result->count);
      result->items = new_items;
      result->capacity = new_cap;
    }

    result->items[result->count++] = VM_OBJ((Obj *)row);
  }

  // step_rc is SQLITE_DONE on a clean finish; anything else (e.g. a residual
  // SQLITE_BUSY the busy handler couldn't resolve) means the row set may be
  // truncated — surface it under TULPAR_DB_DEBUG rather than silently
  // returning a short/empty result.
  if (step_rc != SQLITE_DONE && getenv("TULPAR_DB_DEBUG"))
    fprintf(stderr, "[dbq] step rc=%d (%s) rows=%d\n", step_rc,
            sqlite3_errmsg(db), result->count);

  // Cached statement: reset (releases the read lock / WAL snapshot) instead of
  // finalize, so it stays compiled for the next identical query.
  sqlite3_reset(stmt);

  return VM_OBJ((Obj *)result);
}

VMValue aot_db_query_ptr(VMValue *db_ptr, VMValue *sql_ptr) {
  if (!db_ptr || !sql_ptr)
    return VM_INT(0);
  return aot_db_query(*db_ptr, *sql_ptr);
}

// Bind a Tulpar array of scalars to the `?`/`?N` placeholders of `stmt`
// (1-based). Strings are copied (SQLITE_TRANSIENT) so the VM value may be
// freed/reused afterwards. Non-array params or unsupported element types bind
// NULL. This is what makes parameterized queries injection-safe: values never
// touch the SQL text.
static void db_bind_params(sqlite3_stmt *stmt, VMValue paramsVal) {
  if (!IS_ARRAY(paramsVal))
    return;
  ObjArray *arr = AS_ARRAY(paramsVal);
  for (int i = 0; i < arr->count; i++) {
    VMValue p = arr->items[i];
    int idx = i + 1; // sqlite placeholders are 1-based
    if (IS_INT(p)) {
      sqlite3_bind_int64(stmt, idx, (sqlite3_int64)AS_INT(p));
    } else if (IS_FLOAT(p)) {
      sqlite3_bind_double(stmt, idx, AS_FLOAT(p));
    } else if (IS_BOOL(p)) {
      sqlite3_bind_int(stmt, idx, AS_BOOL(p) ? 1 : 0);
    } else if (IS_STRING(p)) {
      ObjString *s = AS_STRING(p);
      sqlite3_bind_text(stmt, idx, s->chars, s->length, SQLITE_TRANSIENT);
    } else {
      sqlite3_bind_null(stmt, idx);
    }
  }
}

// db_execute(db, sql, params) -> bool. Parameterized variant: `sql` carries
// `?` placeholders, `params` is an array of scalars bound positionally. Same
// success-bool return as the 2-arg form.
VMValue aot_db_execute_params(VMValue dbVal, VMValue sqlVal, VMValue paramsVal) {
  if (!IS_STRING(sqlVal))
    return VM_BOOL(0);
  sqlite3 *db = db_resolve(dbVal);
  if (!db)
    return VM_BOOL(0);
  ObjString *sql = AS_STRING(sqlVal);
  sqlite3_stmt *stmt = db_cached_prepare(db, sql->chars, sql->length);
  if (!stmt) {
    if (getenv("TULPAR_DB_DEBUG"))
      fprintf(stderr, "[dbe] prepare failed (%s)\n", sqlite3_errmsg(db));
    return VM_BOOL(0);
  }
  sqlite3_clear_bindings(stmt);
  db_bind_params(stmt, paramsVal);
  int rc;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    // statement may return rows (e.g. RETURNING); drain them
  }
  bool ok = (rc == SQLITE_DONE);
  if (!ok && getenv("TULPAR_DB_DEBUG"))
    fprintf(stderr, "[dbe] step rc=%d (%s)\n", rc, sqlite3_errmsg(db));
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  return VM_BOOL(ok);
}

VMValue aot_db_execute_params_ptr(VMValue *db_ptr, VMValue *sql_ptr,
                                  VMValue *params_ptr) {
  if (!db_ptr || !sql_ptr)
    return VM_BOOL(0);
  return aot_db_execute_params(*db_ptr, *sql_ptr,
                              params_ptr ? *params_ptr : VM_INT(0));
}

// db_query(db, sql, params) -> array of row objects. Parameterized variant of
// aot_db_query — `?` placeholders bound from the `params` array.
VMValue aot_db_query_params(VMValue dbVal, VMValue sqlVal, VMValue paramsVal) {
  ObjArray *result = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
  result->obj.type = OBJ_ARRAY;
  result->obj.arena_allocated = 1;
  result->obj.next = nullptr;
  result->capacity = 16;
  result->count = 0;
  result->items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * result->capacity);

  if (!IS_STRING(sqlVal))
    return VM_OBJ((Obj *)result);
  sqlite3 *db = db_resolve(dbVal);
  if (!db)
    return VM_OBJ((Obj *)result);
  ObjString *sql = AS_STRING(sqlVal);

  sqlite3_stmt *stmt = db_cached_prepare(db, sql->chars, sql->length);
  if (!stmt) {
    if (getenv("TULPAR_DB_DEBUG"))
      fprintf(stderr, "[dbq] prepare failed (%s)\n", sqlite3_errmsg(db));
    return VM_OBJ((Obj *)result);
  }
  sqlite3_clear_bindings(stmt);
  db_bind_params(stmt, paramsVal);

  int col_count = sqlite3_column_count(stmt);
  int step_rc;
  while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    ObjObject *row = (ObjObject *)aot_arena_alloc(sizeof(ObjObject));
    row->obj.type = OBJ_OBJECT;
    row->obj.arena_allocated = 1;
    row->obj.next = nullptr;
    row->capacity = col_count;
    row->count = 0;
    row->keys = (ObjString **)aot_arena_alloc(sizeof(ObjString *) * col_count);
    row->values = (VMValue *)aot_arena_alloc(sizeof(VMValue) * col_count);
    for (int i = 0; i < col_count; i++) {
      const char *col_name = sqlite3_column_name(stmt, i);
      row->keys[row->count] = aot_allocate_string(col_name, strlen(col_name));
      int col_type = sqlite3_column_type(stmt, i);
      VMValue val;
      switch (col_type) {
      case SQLITE_INTEGER:
        val = VM_INT(sqlite3_column_int64(stmt, i));
        break;
      case SQLITE_FLOAT:
        val = VM_FLOAT(sqlite3_column_double(stmt, i));
        break;
      case SQLITE_TEXT: {
        const char *text = (const char *)sqlite3_column_text(stmt, i);
        int len = sqlite3_column_bytes(stmt, i);
        val = VM_OBJ((Obj *)aot_allocate_string(text, len));
        break;
      }
      case SQLITE_BLOB:
      case SQLITE_NULL:
      default:
        val = VM_INT(0);
        break;
      }
      row->values[row->count] = val;
      row->count++;
    }
    if (result->count >= result->capacity) {
      int new_cap = result->capacity * 2;
      VMValue *new_items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
      memcpy(new_items, result->items, sizeof(VMValue) * result->count);
      result->items = new_items;
      result->capacity = new_cap;
    }
    result->items[result->count++] = VM_OBJ((Obj *)row);
  }
  if (step_rc != SQLITE_DONE && getenv("TULPAR_DB_DEBUG"))
    fprintf(stderr, "[dbq] step rc=%d (%s) rows=%d\n", step_rc,
            sqlite3_errmsg(db), result->count);
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  return VM_OBJ((Obj *)result);
}

VMValue aot_db_query_params_ptr(VMValue *db_ptr, VMValue *sql_ptr,
                                VMValue *params_ptr) {
  if (!db_ptr || !sql_ptr)
    return VM_INT(0);
  return aot_db_query_params(*db_ptr, *sql_ptr,
                            params_ptr ? *params_ptr : VM_INT(0));
}

// db_last_insert_id(db_handle) -> int64
// Resolves to this thread's connection — the same one that ran the INSERT
// within a request handler, so the rowid is correct.
VMValue aot_db_last_insert_id(VMValue dbVal) {
  sqlite3 *db = db_resolve(dbVal);
  if (!db)
    return VM_INT(0);

  return VM_INT(sqlite3_last_insert_rowid(db));
}

// db_error(db_handle) -> string
VMValue aot_db_error(VMValue dbVal) {
  if (!IS_INT(dbVal))
    return VM_OBJ((Obj *)aot_allocate_string("Invalid handle", 14));

  sqlite3 *db = db_resolve(dbVal);
  if (!db)
    return VM_OBJ((Obj *)aot_allocate_string("No database", 11));

  const char *err = sqlite3_errmsg(db);
  return VM_OBJ((Obj *)aot_allocate_string(err, strlen(err)));
}

// ============================================================================
// Type Checking Functions (AOT)
// ============================================================================

VMValue aot_typeof(VMValue v) {
  const char *type_name;
  int len;

  switch (v.type) {
  case VM_VAL_INT:
    type_name = "int";
    len = 3;
    break;
  case VM_VAL_FLOAT:
    type_name = "float";
    len = 5;
    break;
  case VM_VAL_BOOL:
    type_name = "bool";
    len = 4;
    break;
  case VM_VAL_OBJ:
    if (IS_STRING(v)) {
      type_name = "string";
      len = 6;
    } else if (IS_ARRAY(v)) {
      type_name = "array";
      len = 5;
    } else if (IS_OBJECT(v)) {
      type_name = "object";
      len = 6;
    } else {
      type_name = "object";
      len = 6;
    }
    break;
  default:
    type_name = "nullptr";
    len = 4;
  }

  return VM_OBJ((Obj *)aot_allocate_string(type_name, len));
}

VMValue aot_is_int(VMValue v) { return VM_BOOL(IS_INT(v)); }

VMValue aot_is_float(VMValue v) { return VM_BOOL(IS_FLOAT(v)); }

VMValue aot_is_string(VMValue v) { return VM_BOOL(IS_STRING(v)); }

VMValue aot_is_array(VMValue v) { return VM_BOOL(IS_ARRAY(v)); }

VMValue aot_is_object(VMValue v) { return VM_BOOL(IS_OBJECT(v)); }

VMValue aot_is_bool(VMValue v) { return VM_BOOL(IS_BOOL(v)); }

VMValue aot_call_closure(ObjClosure *cls, VMValue *args, int argc) {
  if (!cls) {
    printf("Calisma Zamani Hatasi: Null closure cagirildi\n");
    VMValue res;
    res.type = VM_VAL_VOID;
    return res;
  }
  if (cls->arity != argc) {
    printf("Calisma Zamani Hatasi: Hatali parametre sayisi. Beklenen: %d, Alinan: %d\n", cls->arity, argc);
    VMValue res;
    res.type = VM_VAL_VOID;
    return res;
  }
  VMValue result;
  result.type = VM_VAL_VOID;
  void *env = cls->env;
  switch (argc) {
    case 0:
      ((void(*)(VMValue*, void*))cls->func_ptr)(&result, env);
      break;
    case 1:
      ((void(*)(VMValue*, void*, VMValue*))cls->func_ptr)(&result, env, &args[0]);
      break;
    case 2:
      ((void(*)(VMValue*, void*, VMValue*, VMValue*))cls->func_ptr)(&result, env, &args[0], &args[1]);
      break;
    case 3:
      ((void(*)(VMValue*, void*, VMValue*, VMValue*, VMValue*))cls->func_ptr)(&result, env, &args[0], &args[1], &args[2]);
      break;
    case 4:
      ((void(*)(VMValue*, void*, VMValue*, VMValue*, VMValue*, VMValue*))cls->func_ptr)(&result, env, &args[0], &args[1], &args[2], &args[3]);
      break;
    case 5:
      ((void(*)(VMValue*, void*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*))cls->func_ptr)(&result, env, &args[0], &args[1], &args[2], &args[3], &args[4]);
      break;
    case 6:
      ((void(*)(VMValue*, void*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*))cls->func_ptr)(&result, env, &args[0], &args[1], &args[2], &args[3], &args[4], &args[5]);
      break;
    case 7:
      ((void(*)(VMValue*, void*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*))cls->func_ptr)(&result, env, &args[0], &args[1], &args[2], &args[3], &args[4], &args[5], &args[6]);
      break;
    case 8:
      ((void(*)(VMValue*, void*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*, VMValue*))cls->func_ptr)(&result, env, &args[0], &args[1], &args[2], &args[3], &args[4], &args[5], &args[6], &args[7]);
      break;
    default:
      printf("Calisma Zamani Hatasi: 8'den fazla parametreli closure cagirimi desteklenmiyor\n");
      break;
  }
  return result;
}

#ifdef __cplusplus
}
#endif
