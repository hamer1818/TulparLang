---
tags: [wings, stdlib]
---

# Wings — Serve Modes

Wings **thread-per-connection**. Beş varyant (`lib/wings.tpr`):

| Fonksiyon | Model | Kullanım |
|-----------|-------|----------|
| `serve(port)` → `listen(port)` | tek bağlantı, sync döngü | varsayılan, dev/düşük trafik |
| `listen_pool(port, n)` | çekirdek-başına accept-worker (n≤0 → `cpu_count()`) | CPU-bound handler, max tutarlılık |
| `listen_evented(port)` | tek thread, `poll()` çoğullamalı | çok sayıda hafif keep-alive istemci, en yüksek RPS |
| `listen_async(port)` | bağlantı-başına thread (`thread_create`) | sızıntısız (taze thread) ama thread-başı maliyet |

## Çekirdek fonksiyonlar
- `_wings_serve_connection(client)` — keep-alive döngüsü (pool + async kullanır). Çıkışta `arena_drop(wm)` + `socket_close`.
- `_wings_serve_one_request(client)` — tek istek (evented kullanır). Her çıkışta `arena_drop(wm)`.
- `_wings_dispatch_cached(route_idx, keep)` — cache fast-path + handler + sayaçlar + response build. Her return'de `_wings_last_status` yazar.

## ⚠️ Bellek disiplini
Kalıcı thread'de per-connection/per-request `arena_save` → **`arena_drop` ile serbest bırak** yoksa checkpoint stack tükenir → sızıntı. evented ayrıca `fds` setini yerinde günceller. → [[Memory Leak Fixes]] · [[Memory Model]]

## ✅ Çözülen bug — pool'da ~%1.1 sahte 404 (2026-06-18)
`listen_pool`/`listen_async`'te isteklerin ~%1.1'i 404 dönüyordu. **Kök neden: `toString()` thread-safe değildi** — `aot_to_string` non-TLS `static char aot_string_buffer[1024]` paylaşıyordu; iki worker aynı anda çağırınca yarışıp biri diğerinin baytlarını eziyordu. Belirti: handler'ın `"...id = " + toString(id)` SQL'inde toString **boş** dönüyor → `WHERE id = ` → 0 satır → `not_found()` 404. **Fix: buffer'ı `thread_local` yap** (tek satır). find_route/db_query temizdi — teşhis onları eledi ([noroute]=0, [zero]=0). Doğrulama: pool 107k/80k istek **0 hata**, evented 149k **0 hata**. → [[Wings]] · [[Memory Model]]

## İlgili
[[Wings]] · [[Memory Leak Fixes]] · [[Performance]] · [[Async Runtime]]
