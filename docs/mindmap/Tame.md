---
tags: [component, stdlib, game, tame]
---

# Tame — Oyun Kütüphanesi (2B + 3B)

`import "tame"` → saf Tulpar'dan pencere + 2D çizim + klavye/fare. İsim: **T**ulpar + G**ame**. Wings'in oyun kardeşi — aynı iki-katman modeli (native builtin + gömülü `.tpr` sarmalayıcı). v3.10.0, 2026-07-12.

## Katmanlar
- **Vendored raylib 5.5** — `lib/raylib/` (zlib lisans; [[SQLite and DB]] ile aynı vendor deseni). GLFW'nin X11 uzantı header'ları `lib/raylib/x11_compat/` (MIT). `rcore.c`'de **TULPAR PATCH**: `InitPlatform()` başarısızlığında erken dön (upstream-master davranışı) → headless'ta segfault yerine zarif hata.
- **Native binding** — `runtime/tame_impl.c` (yalnız raylib.h; düz-skaler API + tuş-adı eşleme) + `runtime/tame_bindings.cpp` (yalnız vm.hpp; 114 `aot_tm_*_ptr` (41'i 3B `tm3_*`), N-ptr VMValue ABI). İki-TU ayrımı: raylib.h ↔ windows.h çakışması (Rectangle/CloseWindow/DrawText) imkânsızlaşır.
- **`libtulpar_tame.a`** — raylib + binding'ler, `tulpar_runtime`'dan AYRI arşiv (CMake "Tame" bölümü). Link yalnız `backend->uses_tame` ile: `import "tame"` VEYA doğrudan `tm_*` çağrısı (`tame_link_flags()`, aot_pipeline.cpp; runtime'dan ÖNCE sıralanır). Sıradan binary'ler GL/pencere bağımlılığı almaz; tame binary'si bile X11/GL'i **dlopen'lar** (GLFW modül yükleyici + glad) → ldd'de yoklar, taşınabilir.
- **`lib/tame.tpr`** (gömülü) — kısa isimler + adlı renkler + oyun yardımcıları.

## Codegen
`k_tame_builtins` tablosu (`llvm_backend.cpp`): 114 satır → declare döngüsü + `LLVMGetNamedFunction` dispatch (26 ayrı blok yok). Eksik trailing arg int 0 padlenir. Yeni tame builtin'i = tabloya 1 satır + impl + typeinfer + LSP. → [[AOT Backend]]

## API (kullanıcı yüzü)
- **Döngü (manuel):** `window(w,h,title)` → `while (running()) { frame_begin(); ...; frame_end(); }` → `close_window()`. Varsayılan 60 FPS (`set_fps`), ESC = çıkış (`exit_key(K_NONE)` ile kapatılır — duraklat menüsünün ön koşulu).
- **Pencere kipi (2026-08-27):** `window_resizable(on)` **`window()`'DAN ÖNCE** (raylib bayrağı `InitWindow`'dan önce ister; sonradan kurulan bayrak büyütme düğmesini de ölü bırakır) · `fullscreen(on)` / `is_fullscreen()` — masaüstünde KENARLIKSIZ pencere kipi (gerçek tam ekran monitör çözünürlüğünü değiştirir), web'de tarayıcı tam ekranı, Android'de no-op · `maximize_window(on)` (yalnız resizable pencerede iş görür) · `window_resized()`. Varsayılan KAPALI → [[Decisions]]; [[Scene3D]] açıyor.
- **Döngü (yönetilen, Faz 5):** `run(update_fn, draw_fn)` — wings `listen()` modeli: döngü + kare zamanlaması + **kare belleği** tame'de (döngü öncesi `arena_save`, her kare `arena_restore`, çıkışta `arena_drop`; [[Memory Model]] tek-save-çok-restore). Kalıcı oyun durumu GLOBAL'lerde (wings kuralı). Dispatch: `call(fn, 0)` (group() dummy-arg deseni).
- **Çizim:** `clear/rect/rect_lines/circle/line/pixel/text/triangle` (üçgen sarımı otomatik düzeltilir); `get_fps/frame_time/elapsed/screen_width/screen_height`; `screenshot(path)` (PNG, CWD'ye — raylib yol kırpar).
- **Sprite/font (Faz 3):** `load_texture(path): int` handle (registry, DB-handle deseni; -1=fail), `draw_texture(tex,x,y)`, `draw_texture_ex(tex,x,y,scale,rotation)`, `texture_width/height`, `unload_texture`; `load_font(path,size)` + `text_font`, `measure_text` (ortalama).
- **Ses/müzik (Faz 4):** `load_sound/play_sound/stop_sound/sound_volume`; `load_music/play_music/stop_music/music_volume`. Aygıt ilk yüklemede otomatik açılır; çalan müzik `frame_end()` içinde otomatik pompalanır. `close_window()` tüm kaynakları sıralı bırakır.
- **Girdi:** `key_down/key_pressed/key_released("W"|"SPACE"|"LEFT"|"F1"...)`, `mouse_x/y/down/pressed/wheel` (0=sol 1=sağ 2=orta); **gamepad** `gamepad_available/name/down/pressed/axis(id, "A"|"LB"|"START"..., "LX"|"RT"...)` — PS eş anlamlıları dahil, donanımsız false/""/0.0. `gamepad_name` string döndüren ilk tame builtin'i (`vm_alloc_string_aot`).
- **Renk:** paketlenmiş int `0xRRGGBBAA` — `rgb(r,g,b)`/`rgba(...)` veya 25 adlı raylib rengi (`GOLD`, `SKYBLUE`, ...).
- **Yardımcı:** `rect_overlap` (AABB), `point_in_rect`, `clamp`. Koordinatlar int|float (typeinfer TYPE_UNKNOWN).

## Test / doğrulama
`tame_hello` / `tame_sprite_demo` / `tame_run_demo` — compile-only (build.sh `COMPILE_ONLY_TESTS` — 3.13.0'a kadar `run_tests.ps1` `$compileOnly` ile ikizdi, o PowerShell koşucusu native Windows'la birlikte gitti; artık tek yer): display'li makinede pencere bloklar; derlemeleri import→codegen→link zincirini doğrular. **Pencereli canlı doğrulama (WSLg, 2026-07-12):** tüm çizim türleri + sprite (normal & ölçekli/döndürülmüş) + ortalanmış yazı `tm_screenshot` çıktısıyla piksel-kanıtlı; ters-sarım üçgen auto-fix görüldü; 60 FPS pacing; ses aygıtı açıldı; `run()` 480 kare stabil. Headless: iki dilli hata + exit 0. Test varlıkları `examples/tame_assets/` (python stdlib ile üretildi). build.sh `libtulpar_tame.a`'yı da köke kopyalar. Dikkat: `elapsed()`/`tm_time` `close_window()` sonrası 0 döner (GLFW kapandı) — süreyi pencere açıkken oku.

## Web hedefi (WASM, 2026-07-13)
`tulpar build --target=web oyun.tpr` (veya `--web`) → `oyun.html + .js + .wasm`; tarayıcıda çalışır.
- **Arşivler:** `wasm/build_tame_web.sh` → `wasm/dist/libtulpar_runtime_web.a` (runtime, **async'siz** — ucontext Emscripten'de yok) + `libtulpar_tame_web.a` (raylib PLATFORM_WEB + tame binding'leri; rglfw yok — link `-sUSE_GLFW=3` Emscripten GLFW'sini getirir).
- **Codegen:** `backend->target_web` (declare'den ÖNCE set edilir) → emit wasm32-unknown-emscripten. ⚠️ **ABI:** wasm32'de VMValue C ABI'si SysV `{i64,i64}` DEĞİL — Win64 gibi **sret+byval**; `vmvalue_abi_uses_sret()` (llvm_values.cpp) çalışma zamanında seçer. CMake her mimaride WebAssembly LLVM bileşenlerini bağlar.
- **Link:** em++ `-O2 -sUSE_GLFW=3 -sASYNCIFY -sALLOW_MEMORY_GROWTH` — ASYNCIFY sayesinde Tulpar'ın bloklu `while (running())` döngüsü tarayıcı event-loop'una çevrilir (raylib web EndDrawing → `emscripten_sleep`). Varlıklar: `TULPAR_WEB_ASSETS=<dizin>` → `--preload-file`.
- ⚠️ **v1 web sınırı — `call()`/`run()` çalışmaz:** `call(name)` dlsym'e dayanır; statik wasm'da dinamik sembol araması yok (Emscripten dlsym MAIN_MODULE ister). `run(update, draw)` ve wings-tarzı fonksiyon-ref dispatch web'de v1'de desteklenmez — **manuel döngü kullan** (`examples/tame_web_mini.tpr`). Kalıcı çözüm adayı (roadmap): codegen tüm kullanıcı fonksiyonlarını bildiğinden statik isim→işaretçi tablosu emit edip `aot_call_function`'ın önce ona bakması — native'de -rdynamic ihtiyacını da kaldırır.
- **ASYNCIFY stack:** varsayılan 4KB, Tulpar main'inin hoisted VMValue local'leriyle taşar ("Aborted(Asyncify stack overflow)") → link `-sASYNCIFY_STACK_SIZE=131072` kullanır.
- **emsdk:** vendored (`wasm/emsdk`, 5.0.0). Tuzak: repo kopyalanınca `upstream/bin` symlink'leri (clang→clang-23, wasm-ld→lld) kaybolabilir — "clang executable not found" = symlink'leri yeniden kur. Kullanım: `source wasm/emsdk/emsdk_env.sh`.
- Eski `wasm/CMakeLists.txt` playground'u ölü (silinmiş VM dosyalarına referans) — Tame web yolundan bağımsız, diriltilmeyecek.

## 3B ve preset katmanları
Tame bağlama katmanıdır; oyun yazarken genelde doğrudan kullanılmaz:
- [[Arcade]] — 2B preset motoru (`import "arcade"`, saf Tulpar)
- [[Scene3D]] — 3B motor (`import "scene3d"`, saf Tulpar): entity, çarpışma, kamera,
  arazi, gündüz-gece, tetikleyici bölge, kalıcılık
- [[Android]] — native APK hedefi (web hedefinin kardeşi)

3B bağlamaları `tm3_*` önekli (kamera, ışık, gölge, gökyüzü, sis, model/animasyon,
arazi heightmap + katman boyama, ışın-kutu seçimi). Işık shader'ı `tame_impl.c`
içinde gömülü GLSL, iki varyant (GLES2 + GL3.3); alfa ve tepe rengi davranışı
[[Scene3D]] notunda.

## Açık işler
Gamepad ✅, WASM kablolaması ✅ (2026-07-13). → [[Roadmap]]

## İlgili
[[Architecture]] · [[AOT Backend]] · [[Memory Model]] · [[Standard Library]] · [[Wings]] (model kardeşi)
