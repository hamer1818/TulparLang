---
tags: [wings, stdlib, ui]
---

# Wings — Renkli Access Log

Format: `  [HH:MM:SS] METHOD STATUS /path  → latency  - size` — hepsi renkli (`lib/wings.tpr`).

## Renkler
- timestamp dim; **method**'a göre (GET yeşil, POST sarı, PUT mavi, DELETE kırmızı...); **status** sınıfına göre (2xx yeşil, 3xx cyan, 4xx sarı, 5xx kırmızı); **latency** eşiğe göre (<50ms yeşil, <500ms sarı, ≥500ms kırmızı); size dim.
- `_wings_c(code, text)` SGR sarmalayıcısı; **NO_COLOR** env renkleri kapatır (`_wings_color`).

## Yardımcılar
- `_wings_now_hms()` — `now_iso8601()`'den `substring(...,11,19)`.
- `_wings_fmt_ms(ms)` — **adaptif**: <1ms ise mikrosaniye (`34µs`) yoksa `12.4ms`. AOT sunucusu o kadar hızlı ki ms'de 0.0 çıkıyordu.
- `_wings_fmt_bytes(n)` — B/KB/MB.
- `_wings_log_request(method, path, status, ms, bytes)`.

## ⚠️ Dil tuzakları (burada öğrenildi)
- Tulpar'da **`%` operatörü yok** → `mod(a,b)` / `fmod`.
- **`/` float operandla float bölme** yapar (`int t = floor(x)` bile runtime'da float kalır) → gerçek int bölme için `toInt()`. → [[Decisions]]
- Latency için `floor` yerine `round` (float repr: `12.4*10=123.999`).

## Mekanizma
Status handler'dan sonra bilindiği için log, response belirlendikten **sonra** basılır (`_wings_last_status`). 3 serve döngüsünde de.

## İlgili
[[Wings]] · [[Lexer]] (`\e` escape) · [[Decisions]]
