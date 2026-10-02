// Veritabani yuzeyi: SQLite yerlesikleri (db_open/db_query/... ve ORM'in
// dayandigi parametreli sorgular).
//
// NEDEN AYRI BIR DERLEME BIRIMI (K214, runtime_net.cpp ile ayni sebep):
// `runtime_bindings.cpp` HER AOT ikilisine giriyor. SQLite cagrilari orada
// durdugu surece baglayici `sqlite3.o`yu da HER ikiliye aliyordu — `print(1)`
// yazan program bile 636 KB SQLite tasiyordu (Tuzaklar 6s, Performance.md
// "sirada duran"). Olculdu 2026-09-27 (x86_64, GCC 15, `tulpar build`):
// bos program 3 005 736 bayt, ikilide 293 `sqlite3_*` sembolu (652 949 bayt).
// Ayri birimde durunca bu nesne — ve onunla sqlite3.o — YALNIZ program bir
// db_* yerlesigi cagirdiginda iceri giriyor.
//
// Buraya SQLite'a dokunan her yeni yerlesik gelmeli; runtime_bindings.cpp'ye
// `sqlite3_` yazmak ayrimi sessizce geri alir (tests/source_gates.py bekler).
#include "../../lib/sqlite3/sqlite3.h"
#include "../common/platform.h"
#include "vm.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

// runtime_bindings.cpp'de tanimli arena yardimcilari.
ObjString *aot_allocate_string(const char *chars, int length);
void *aot_arena_alloc(size_t size);

// ============================================================================
// SQLite Database Functions (AOT)
// ============================================================================

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
// same SELECT repeatedly. The key is the SQL text: `db_query_params` binds
// `?` placeholders after the cached prepare, so one parameterised statement
// serves every value; SQL with values baked into the text (plain `db_query`)
// gets one entry per distinct string. (This comment used to say "no bound
// params" — true before `db_query_params` existed.)
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
    arr->idata = nullptr;
    arr->obj.arena_allocated = 1;
    arr->capacity = 0;
    arr->count = 0;
    arr->items_ = nullptr;
    return VM_OBJ((Obj *)arr);
  }

  sqlite3 *db = db_resolve(dbVal);
  ObjString *sql = AS_STRING(sqlVal);

  // Prepare result array
  ObjArray *result = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
  result->obj.type = OBJ_ARRAY;
  result->idata = nullptr;
  result->obj.arena_allocated = 1;
  result->capacity = 16;
  result->count = 0;
  result->items_ =
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
    row->capacity = col_count;
    row->count = 0;
    row->index = nullptr;
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
      memcpy(new_items, arr_items(result), sizeof(VMValue) * result->count);
      result->items_ = new_items;
      result->capacity = new_cap;
    }

    arr_items(result)[result->count++] = VM_OBJ((Obj *)row);
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
    VMValue p = arr_items(arr)[i];
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
  result->idata = nullptr;
  result->obj.arena_allocated = 1;
  result->capacity = 16;
  result->count = 0;
  result->items_ = (VMValue *)aot_arena_alloc(sizeof(VMValue) * result->capacity);

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
    row->capacity = col_count;
    row->count = 0;
    row->index = nullptr;
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
      memcpy(new_items, arr_items(result), sizeof(VMValue) * result->count);
      result->items_ = new_items;
      result->capacity = new_cap;
    }
    arr_items(result)[result->count++] = VM_OBJ((Obj *)row);
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

#ifdef __cplusplus
}
#endif
