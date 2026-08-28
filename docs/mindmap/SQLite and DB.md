---
tags: [component, runtime, db]
---

# SQLite & DB

SQLite (`lib/sqlite3/sqlite3.c`) vendored, hem `tulpar` hem `tulpar_runtime` içine derlenir. Builtin'ler: `runtime_bindings.cpp` (`aot_db_*`).

## API (Tulpar tarafı)
- `db_open(path) -> int handle` — handle artık ham pointer DEĞİL, 1-tabanlı `DbConn` descriptor index'i (API aynı, int64).
- `db_query(handle, sql) -> array<json>` (SELECT, satırlar obje)
- `db_execute(handle, sql) -> bool` (INSERT/UPDATE/DELETE; PRAGMA da çalışır)
- `db_last_insert_id(handle) -> int`, `db_error(handle) -> str`, `db_close(handle)`

## Bilinmesi gerekenler (2026-06-18)
- **`db_open` server-dostu varsayılanlar:** `busy_timeout=5000` + dosya-tabanlı DB'lerde **WAL + synchronous=NORMAL**. Stres testinde write **8.8k → 20.4k RPS (2.3×)**. `:memory:` atlanır; opt-out `TULPAR_DB_NO_WAL=1`.
- **Thread/bağlantı-başına handle (✅ yapıldı, ölçüldü):** dosya-tabanlı DB'de **her thread kendi `sqlite3` bağlantısını lazily açar** (`db_resolve` + `thread_local` cache; `g_db_registry` descriptor tablosu). WAL altında çok okuyucu **paralel**. **Ölçüm: read-by-PK pool 23.8k → 35.1k RPS (+~47%)**, mutex darboğazı kalktı (14 worker = 14 ayrı bağlantı doğrulandı). Evented (tek-thread) 37.3k — kalan fark scheduling overhead'i, mutex değil. `:memory:`/temp tek paylaşımlı bağlantıda kalır. `db_last_insert_id` çağıran thread'in bağlantısına çözülür (INSERT ile aynı thread → doğru rowid). `db_close` idempotent + `closed` bayrağı. **Bellek:** per-bağlantı ~40 KB; baseline **9.36 MB** → 14 worker sonrası **9.9 MB düz** (sızıntı yok). → [[Performance]]
- ⚠️⚠️ **Test tuzakları (bunlar yüzünden saatler kaybettim):** (1) Gerçek sunucu RSS'i AOT child `/tmp/.tulpar_run`'da; `./tulpar` driver'ı LLVM yüzünden ~62 MB gösterir. (2) **AOT linker repo kökündeki `./libtulpar_runtime.a`'yı önce linkler** — `runtime_bindings.cpp` değişince `cp build-linux/tulpar ./tulpar && cp build-linux/libtulpar_runtime.a ./libtulpar_runtime.a` (ikisi de!), yoksa stale runtime test edilir. `build.sh` otomatik yapar. → [[Build System]]
- `lib/orm.tpr` tek global `_orm_db` kullanır — artık paralel read'den otomatik faydalanır (kod değişmeden).
- `db_last_insert_id`/`db_error` codegen imza fix'i (by-value), eskiden module verification uyarısı basıyordu. → [[AOT Backend]]

## Açık iş
DB tarafı paralel-read kapandı. Kalan polish: per-connection `cache_size` ile RSS sınırlama (opsiyonel), prepared-statement cache.

## İlgili
[[Runtime]] · [[ORM]] · [[Performance]] · [[Roadmap]]
