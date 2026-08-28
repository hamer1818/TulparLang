---
tags: [runtime, memory, history]
---

# Memory Leak Fixes (Wings hot path)

İki ayrı sızıntı bulundu ve çözüldü; ikisi de stres/benchmark ile yakalandı.

## 1) Per-request sızıntısı (2026-06-17)
Salt-okuma `GET` altında bile RSS lineer büyüyordu (~2.6 KB/istek). Sebep: AOT'ta obje/dizi literalleri `malloc`'lanıp hiç free edilmiyordu (arena yalnız string geri alıyordu). Çözüm: **per-request malloc region + runtime write barrier** ([[Memory Model]]). Sonuç: RSS düz, ASan temiz.

## 2) Bağlantı-churn sızıntısı (2026-06-18)
`Connection: close` (kısa ömürlü bağlantı) altında pool RSS **9.1 GB**'a fırladı. Kök neden: 32-slot checkpoint stack; `listen_pool` (bağlantı-başına `arena_save`) ve `listen_evented` (istek-başına) kalıcı thread'de checkpoint'i serbest bırakmıyor → 32'den sonra `arena_save → -1`, restore no-op → her şey sızıyor. Çözüm: **`arena_drop(wm)`** (rewind + pop). serve fonksiyonları çıkışta drop; `listen_evented` ayrıca her poll tick'ini scope'layıp kalıcı `fds` dizisini yerinde günceller. → [[Memory Model]] · [[Wings Serve Modes]]

Sonuç: pool 481k istekte **8.4 MB düz**, evented 293k istekte **6.9 MB düz**. Varsayılan `serve()` zaten etkilenmiyordu (döngü dışında tek save).

## İlgili
[[Memory Model]] · [[Wings Serve Modes]] · [[Performance]]
