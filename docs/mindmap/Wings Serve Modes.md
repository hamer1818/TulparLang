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

## ✅ evented: bloklamayan YAZMA, pipelining, çok çekirdek (2026-11-26)
- **Yazma:** olay döngüsü yanıtı `http_send_queued(fd, veri, kapat)` ile yazar — soketin aldığı
  kadar gider, kalan fd başına çıktı tamponunda bekler; `http_poll` (socket_poll'un yerine)
  kuyruklu fd'ler için POLLOUT ister ve boşaltır; keep-alive olmayan bağlantıyı **runtime**
  boşalınca kapatır (`_wings_serve_one_request` 2 döner: "poll'dan çıkar, kapatma"). Eskiden
  yanıtını okumayan tek istemci (büyük yanıt) `socket_send`'de bütün döngüyü durduruyordu.
- **Pipelining:** bloklamayan okuyucu aynı `recv`'de gelen İKİNCİ isteği yutuyordu (iki istek
  tek dizgi, ayrıştırıcı ilkini görüp ikincisini atıyordu). Artık fazlası fd tamponunda kalır,
  `http_poll` o fd'yi hemen hazır sayar.
- **Çok çekirdek:** `evented_workers(n)` / `TULPAR_WINGS_WORKERS=n` — n olay döngüsü (iş
  parçacığı) aynı bloklamayan dinleme soketini paylaşır; kabul eden döngü bağlantının sahibi
  (poll kümesi, yarım istek, çıktı kuyruğu iş parçacığına özel). Sayaçlar atomik, istek başına
  durum thread-local (`_request`, `_wings_deps`, `_wings_last_status` — sonuncusu bu turda
  `@thread_local` oldu; pool'da da bir iş parçacığı diğerinin durum kodunu logluyordu).
- Kapı `tests/wings_yazma_smoke.py`: yavaş okuyucu varken /ping hemen (kontrol: bloklu `listen()`
  kilitli), 8 MB eksiksiz + kapanış, pipelining sırası, 4 işçide CPU yoğun 4 istek 1.0x (kontrol:
  1 işçide 4.0x).

### Gecikme histogramı (2026-11-26, Ryzen 7 9800X3D, `tools/wings_gecikme.py`)
32 keep-alive istemci × 200 `GET /ping`; **kötü** = aynı yük + 2 yavaş okuyucu (8 MB isteyip
okumayan) + 2 slowloris. Çalışma başına 10 s tavan. İstemci Python: mutlak RPS istemcinin
tavanı (~140k), modlar arası FARK anlamlı.

| mod | yük | p50 ms | p99 ms | maks ms | RPS | tamamlanmadı |
|---|---|---:|---:|---:|---:|---:|
| listen | düz | 0.01 | 0.04 | 58.9 | 100 902 | 0 |
| listen | **kötü** | — | — | — | 0 | **6400/6400** (kilit) |
| listen_pool(4) | düz | 0.02 | 0.10 | 31.6 | 165 044 | 0 |
| listen_pool(4) | **kötü** | 0.01 | 0.04 | 635.8 | **2 409** | 32 |
| listen_async | düz | 0.17 | 0.77 | 2.0 | 135 366 | 0 |
| listen_async | kötü | 0.17 | 0.73 | 1.9 | 138 420 | 0 |
| listen_evented | düz | 0.17 | 0.70 | 2.1 | 143 334 | 0 |
| listen_evented | kötü | 0.18 | 0.68 | 1.0 | 141 398 | 0 |
| listen_evented ×4 | düz | 0.17 | 0.71 | 1.4 | 139 488 | 0 |
| listen_evented ×4 | kötü | 0.17 | 0.69 | 1.6 | 140 446 | 0 |

Okuma: kötü istemci bloklayan modları **durduruyor** (listen: tamamen; pool(4): 4 işçinin 4'ü
kötülerle meşgul → RPS 70× düşüyor). async ve evented etkilenmiyor. Boş işleyicide ×4 ile ×1
aynı — darboğaz istemci; çok çekirdeğin kazancı CPU yoğun işleyicide (kapıda 4.0× → 1.0×).

## ✅ Çözülen bug — pool'da ~%1.1 sahte 404 (2026-06-18)
`listen_pool`/`listen_async`'te isteklerin ~%1.1'i 404 dönüyordu. **Kök neden: `toString()` thread-safe değildi** — `aot_to_string` non-TLS `static char aot_string_buffer[1024]` paylaşıyordu; iki worker aynı anda çağırınca yarışıp biri diğerinin baytlarını eziyordu. Belirti: handler'ın `"...id = " + toString(id)` SQL'inde toString **boş** dönüyor → `WHERE id = ` → 0 satır → `not_found()` 404. **Fix: buffer'ı `thread_local` yap** (tek satır). find_route/db_query temizdi — teşhis onları eledi ([noroute]=0, [zero]=0). Doğrulama: pool 107k/80k istek **0 hata**, evented 149k **0 hata**. → [[Wings]] · [[Memory Model]]

## İlgili
[[Wings]] · [[Memory Leak Fixes]] · [[Performance]] · [[Async Runtime]]
