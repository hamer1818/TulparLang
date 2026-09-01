---
tags: [moc, games, 2d]
---

# Arcade — 2B Oyun Preset Katmanı

`import "arcade"` — [[Tame]] üzerine kurulu ~800 satır **saf Tulpar** preset motoru
(`lib/arcade.tpr`). C/runtime bağlaması yok → düzenlemek için `./build.sh clean` yeterli.

Yayınlanan 10 tarayıcı oyununun kullandığı katman (`examples/arcade_*.tpr` +
`examples/tame_snake.tpr`, her birinin `examples/en/` altında İngilizce ikizi).

## Taşıyıcı kısıtlar
- **Entity id'leri generation-etiketli handle**, dizi indeksi DEĞİL:
  `_egen[slot]*1048576 + slot`, `_slot_of()` ile çözülür. Paralel diziyi
  (`_evx[id]`) ham handle ile indeksleme YASAK — getter'lardan geç (`vx_of`, `get_x`).
  (arcade paralel dizi kullanıyor; [[Scene3D]] daha sonra `struct Ent3` dizisine geçti.)
- **Bölüm geçişleri ertelenir** (`_lvl_pending`, kare sonunda uygulanır) — böylece
  `next_level()` çarpışma yinelemesinin ortasında entity dizilerini değiştiremez.
  Bu dil kısıtı değil, **doğru oyun döngüsü tasarımı**.
- **`call(fn, a, b, …)` N argüman iletir** (2026-07-20 düzeltildi; öncesi segfault'tu,
  bu yüzden 0-argümanlı callback + global bağlam deseni vardı).
- **Rezerve kelime tuzağı:** `move` ve `don` (=`return`) token'dır → `bool don = ...`
  modülü sessizce bozar. Aynı aileden: `dene` (=`try`). → [[Roadmap]]

## Dokunmatik
Arcade, tame'in dokunmatik bağlamalarını ekran D-pad + aksiyon düğmesine bağlar
(ilk dokunuşta otomatik açılır, `touch_controls()` ile zorlanır). Hareket preset'leri
klavye VEYA dokunmatik okur → masaüstü değişmez. 10 oyunun hepsi cihazda oynanabilir.

## Yayın
`tulpar build --target=web examples/arcade_zipla.tpr -o out/jump` → `.html/.js/.wasm`.
HTML kabuğu (`write_web_shell_html()`, aot_pipeline.cpp) `pointer:coarse` cihazlarda
**dokunmatik gamepad** gösterir ve raylib'e sentetik `KeyboardEvent` üretir — her web
oyunu mobil kontrolü bedava alır. Yayın: `tulparlang.dev/oyunlar/`.

## İlgili
[[Tame]] · [[Scene3D]] · [[Android]] · [[Standard Library]]
