---
tags: [build, infra]
---

# Build System

CMake 3.14+ + **LLVM 18–22** (hard requirement). C++17 zorunlu. CMake `TULPAR_LLVM_MAJOR`
değişkenini açıyor ve codegen, LLVM'in sürümler arasında yeniden adlandırdığı API
yazımlarını buna göre `#if`'liyor — tek bir sürümü sabitleme.

## Komutlar
- `./build.sh` — Linux/macOS, `build-linux/` veya `build-macos/`'da derler, `./tulpar`'ı
  repo köküne kopyalar. **Her çağrıda `$BUILD_DIR` siler** (incremental yok).
- `./build.sh clean` — build dizinleri + artefaktları sil.
- `./build.sh test` — `examples/*.tpr` üzerinde e2e (AOT → çalıştır → exit status).
  `COMPILE_ONLY_TESTS` listen/api_run bloklayan (ve pencere açan) örnekleri yalnız derler.
- `./build.sh suites` — `tests/*.test.tpr` paketleri (59) **+ denetimler**: builtin,
  kama mesh, dist arşiv, LSP, fmt, doc, pkg, Android derleme dumanı, ayrılmış kelime ve
  parametre adı tanılamaları, `packages/wings_jwt` ve **kod üretimi denkliği** (iki sahne).
  Özet satırı (`Tests:`) basmayan paket **hata** sayılıyor — `test_summary()` çağırmayan
  bir paket asla kırmızı olamazdı. **Paket döngüsü hata bulursa denetimlere hiç
  gelinmiyor** (`exit 1`), yani tek bir kırmızı paket denetimleri de gizler. → [[Testing]]
- Incremental için doğrudan `cmake -S . -B build-linux && cmake --build build-linux -j`.

> ⚠️ Bellek: `build.sh` koşumların tepe kullanımını raporluyor. Ölçüldü (2026-09-01):
> `test` tek başına **~19 GB** zirve (52 örnek paralel derleniyor), `suites` ~12 GB.
> İkisini aynı anda başlatma — OOM ile öldürülebilir; **ayrı komutlarda** çalıştır.
> → [[Tuzaklar]] §7

## Native Windows YOK (3.13.0'da düşürüldü)
`build.bat` / `build.ps1` / `run_tests.ps1` / Inno Setup installer ve `build-windows` CI
işi **yok**. Windows'ta geliştirme **WSL** içinde, yukarıdaki Linux yolundan yapılıyor —
web ve Android hedefleri dahil her şey orada çalışıyor. Shim'lerdeki `PLATFORM_WINDOWS`
dalları bilerek bırakıldı ama **bakımsız ve sınanmamış**. → [[Cross-platform]]

## Üç hedef
`tulpar` (derleyici) · `tulpar_runtime` (static lib, `-DTULPAR_RUNTIME_ONLY`, AOT
binary'lerin linklediği) · `tulpar_tame` (vendored raylib + `aot_tm_*` bağlamaları,
yalnız `tame`/`tm_*` kullanan programa linkleniyor). → [[Runtime]] · [[Tame]]

## `tulpar build` önbelleği
Çıktı ikilisi kaynaktan VE sürücüden yeniyse bütün AOT hattı atlanıyor
(`[AOT] Cache hit`). `TULPAR_AOT_NOCACHE=1` ile kapanır; web/Android hedeflerinde zaten
atlanmıyor (üretilen şey `output_name`'in kendisi değil).

> ⚠️ **Import edilen modüller uzun süre hesaba katılmıyordu** (2026-09-01'de düzeltildi).
> `import "lib/scene3d"` gibi YEREL bir modülü düzeltip yeniden derlemek "Cache hit" alıp
> **sessizce eski ikiliyi** bırakıyordu — belirti çok yanıltıcı: *düzeltmen işe yaramamış
> görünüyor*. Artık `newest_local_import_mtime()` (`src/main.cpp`) import'ları arka uçla
> AYNI sırayla çözüp özyinelemeli tarıyor. Gömülü stdlib adları diskte çözülmez; onları
> sürücünün mtime'ı kapsıyor. → [[Tuzaklar]] §2

## ⚠️ Sürüm numarası ÖNBELLEKLİ
`TULPAR_VERSION` bir CMake **cache** değişkeni (`set(... CACHE STRING ...)`).
`project(VERSION ...)` yükseltilip **aynı build dizininde** yeniden derlenirse
eski değer kalır ve `tulpar version` yanlış sürümü söyler — sessizce. Ölçüldü
(2026-09-02, v3.13.1 keserken): 3.13.1'e yükseltildikten sonra ikili hâlâ
`3.13.0-dev` diyordu. CI'da görünmez (her koşum temiz dizin; etiket koşumu
ayrıca `-DTULPAR_VERSION` ile eziyor), yani yalnız yereli yanıltır.

**Sürüm yükselttikten sonra:** ya build dizinini sil, ya da
`cmake -S . -B build-linux -DTULPAR_VERSION=<yeni>-dev` ile ez.

## ⚠️ Gömülü lib değişikliği RECONFIGURE ister
`lib/*.tpr` → `embedded_libs.h` (`EmbedLibraries.cmake`, `configure_file`). Değişikliği
görmek için `cmake -S . -B build-linux` **yeniden yapılandırma** şart; yalnız `--build`
yetmez. `src/embedded_libs.h` üretilen çıktıdır (gitignore) — elle düzenleme, şablon
`src/embedded_libs.h.in` ve `lib/*.tpr` düzenlenir. SQLite `lib/sqlite3/sqlite3.c` hem
`tulpar`'a hem `tulpar_runtime`'a derlenir. → [[Standard Library]] · [[SQLite and DB]]

## ⚠️ WSL stale-build
Incremental build saat kayması yüzünden stale obje kullanabiliyor — özellikle `lib/*.tpr`
değişince. **`./build.sh clean` yap.** Belirti: kaynak/`embedded_libs.h` yeni ama binary
eski davranıyor, ya da LLVM "Incorrect number of arguments". Kökteki bayat `.a` arşivleri
de taze derlemeyi gölgeler (`cp build-linux/libtulpar_*.a ./`).

## CI — main'in gerçek hâli test ediliyor mu?
İki ayar birlikte bir delik açıyor ve ikisi de tek başına makul görünüyor:

| Ayar | Ne yapıyor |
|---|---|
| `required_status_checks.strict` **kapalı** | PR **eski** bir main'e karşı yeşile dönebiliyor |
| main-push işinde **artefakt yeniden kullanımı** | Reuse başarılıysa derleme/test/süit/duman/paketleme adımlarının **hepsi atlanıyor** |

Birlikte: **main'in birleşmiş hâlini hiçbir şey sınamıyor.** İki PR ayrı ayrı
yeşil olup birleşince main'i bozabilir (biri bir fonksiyonu siler, öteki onu
çağırır) ve CI yeşil kalır. Ölçüldü (2026-09-02): main-push koşumunda
`Build with LLVM`, `Run tests`, `Run tests/*.test.tpr suites`,
`AOT end-to-end smoke`, `Package TameEngine` — hepsi **skipped**.

**Çözüm `strict`i açmak.** Açıkken dal main'i içeriyor demektir; squash sonrası
main'in ağacı PR'ın ağacına EŞİT olur, yani yeniden kullanma *güvenilir* hâle
gelir. Yani reuse iyileştirmesi zaten `strict`i varsayıyordu — o varsayım
yazılı değildi ve tutmuyordu.

```bash
gh api -X PATCH repos/<sahip>/<depo>/branches/main/protection/required_status_checks \
  --input - <<< '{"strict":true,"contexts":["build-linux","build-macos"]}'
```

Alternatif (strict istenmiyorsa): main-push işinde yeniden kullanmayı kapat ve
testleri gerçekten koştur — önleme yerine tespit, merge başına ~20 dk.
**İkisinden biri olmalı; hiçbiri olmazsa main sınanmamış demektir.**

## CI
`.github/workflows/build.yml` — Ubuntu + macOS. Test adımlarını **yalnız Linux işi**
koşuyor: `./build.sh test`, `./build.sh suites`, typeinfer koşucusu, SHA-256 yardımcısı.
Hiçbiri `continue-on-error` **değil**: bir test hatası CI'yı kırmızıya çeviriyor.
Windows işi yok (yukarı bak).

## İlgili
[[Standard Library]] · [[Runtime]] · [[Cross-platform]] · [[AOT Backend]] · [[Testing]] · [[Tuzaklar]]
