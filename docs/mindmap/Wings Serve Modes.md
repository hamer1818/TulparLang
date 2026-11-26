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
- `_wings_serve_one_request(client)` — tek istek (evented kullanır), bloklamayan okuma; istek eksikse 1 döner (fd poll kümesinde kalır). Her çıkışta `arena_drop(wm)`.
- `_wings_dispatch_cached(route_idx, keep)` — cache fast-path + handler + sayaçlar + response build. Her return'de `_wings_last_status` yazar.

## ⚠️ Bellek disiplini
Kalıcı thread'de per-connection/per-request `arena_save` → **`arena_drop` ile serbest bırak** yoksa checkpoint stack tükenir → sızıntı. evented ayrıca `fds` setini yerinde günceller. → [[Memory Leak Fixes]] · [[Memory Model]]

## ✅ Slowloris — okuma süresi sınırı (2026-11-26)
Eskiden `http_recv_request` recv'i **sınırsız** bekliyordu: başlığı damla damla gönderen istemci (200 ms'de bir bayt — recv başına zaman aşımı bunu yakalamaz) işçiyi sonsuza dek tutuyordu. `listen_pool(port, N)`'de N yavaş istemci sunucuyu tamamen durduruyordu; `listen_evented`'de **tek** yavaş istemci bütün döngüyü (tek iplik okurken bloklanıyordu).
- `read_timeout(ms)` (varsayılan **10 000**, `0` = sınırsız, `TULPAR_WINGS_READ_TIMEOUT_MS` ezer) → runtime'da süreç geneli `http_read_timeout`. Başlık bloğu (keep-alive'da yeni isteğin ilk baytını beklemek dahil) **toplam** `ms` içinde gelmeli; gövde iki okuma arasında en fazla `ms` durabilir (ilerleyen yavaş yükleme kesilmez). Bayt geldiyse **408**, gelmediyse sessiz kapanış. Soketin `SO_RCVTIMEO`'su (graceful `listen()` 2 s) poll'a uygulanmaya devam ediyor.
- `listen_evented` artık **bloklamadan** okuyor: `http_recv_ready` o an bekleyen baytları alır, yarım isteği fd başına runtime tamponunda tutar; tick başındaki `http_recv_expired()` süresi dolanlara 408 yazıp kapatır. `socket_close` tamponu siler (fd numarası yeniden kullanılınca karışmasın). Bosta keep-alive bağlantılar evented'de zaman aşımına uğramaz (modun amacı bu).
- Kapı `tests/wings_slowloris_smoke.py` (`build.sh suites`): dört mod, yavaş istemci 408 + kapanış, diğer istekler geçer, yavaş gövde kesilmez. Kontrol: süre 0 iken pool **kilitlenmeli**; evented bloklayan okumaya geri döndürülünce kırmızı (4 yavaş istemci = 4,5 s durma — yalnız süre sınırı evented'i kurtarmazdı).
- Kalan: gövdeyi `ms`'den kısa aralıklarla damlatan istemci (R.U.D.Y.) bir işçiyi uzun süre tutabilir (asgari hız kuralı yok); yanıtı okumayan istemciye `socket_send` bloklar; `lib/router.tpr` ve `wings_tls` kendi okuyucularını kullanıyor, süre sınırı onlara uygulanmıyor.

## ✅ Çözülen bug — pool'da ~%1.1 sahte 404 (2026-06-18)
`listen_pool`/`listen_async`'te isteklerin ~%1.1'i 404 dönüyordu. **Kök neden: `toString()` thread-safe değildi** — `aot_to_string` non-TLS `static char aot_string_buffer[1024]` paylaşıyordu; iki worker aynı anda çağırınca yarışıp biri diğerinin baytlarını eziyordu. Belirti: handler'ın `"...id = " + toString(id)` SQL'inde toString **boş** dönüyor → `WHERE id = ` → 0 satır → `not_found()` 404. **Fix: buffer'ı `thread_local` yap** (tek satır). find_route/db_query temizdi — teşhis onları eledi ([noroute]=0, [zero]=0). Doğrulama: pool 107k/80k istek **0 hata**, evented 149k **0 hata**. → [[Wings]] · [[Memory Model]]

## İlgili
[[Wings]] · [[Memory Leak Fixes]] · [[Performance]] · [[Async Runtime]]
