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

## Native Windows GERİ GELDİ (2026-09-21, MSYS2 MINGW64)
3.13.0'da düşürülmüştü; #340–#342 ile `build-windows` CI işi, testler ve Inno Setup
kurulumcusu geri geldi. Derleme MSYS2 MINGW64 kabuğunda `./build.sh` ile (paket listesi
CLAUDE.md "Build"). `build.bat` / `build.ps1` / `run_tests.ps1` YOK ve gelmedi — `build.sh`
Windows'ta da tek giriş. Shim'lerdeki `PLATFORM_WINDOWS` dalları artık derleniyor ve
koşuyor. Windows'a özgü üç tuzak [[Tuzaklar]] §3g'de. → [[Cross-platform]]

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

## Gömülü lib: `cmake --build` yeter (2026-09-27'den beri)
`lib/*.tpr` → `embedded_libs.h` (`EmbedLibraries.cmake`, `configure_file`). İçerik
YAPILANDIRMA anında okunuyor; 2026-09-27'ye kadar derleme sistemi bu dosyaları
izlemiyordu, yani `cmake --build` bir stdlib değişikliğini **sessizce görmüyor** ve
ikili eski kütüphaneyi taşıyordu (yeniden yapılandırma şarttı). Artık her gömülü
dosya `CMAKE_CONFIGURE_DEPENDS`'e giriyor: değişirse cmake kendiliğinden yeniden
koşuyor, başlık yeniden yazılıyor, onu içeren üç TU yeniden derleniyor.
Kapı: `tests/gomulu_stdlib_tazelik.py` (`build.sh suites`'in İLK adımı) — derleme
sistemi bağımlılığı + `./tulpar`'ın her lib dosyasını AYNEN taşıması; bayat bir
`./tulpar` ile paketler koşmaz. (CRLF'li lib dosyaları ikiliye LF'li gömülüyor —
CMake CR'yi düşürüyor; kapı bunu normalize ediyor.)

`src/embedded_libs.h` üretilen çıktıdır (gitignore) — elle düzenleme, şablon
`src/embedded_libs.h.in` ve `lib/*.tpr` düzenlenir. **Yeni** bir lib dosyası hâlâ
`EmbedLibraries.cmake` + şablonda bir yuva ister. SQLite `lib/sqlite3/sqlite3.c` hem
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

**✅ ÇÖZÜLDÜ (2026-09-02): `strict` açıldı.** Açıkken dal main'i içeriyor
demektir; squash sonrası main'in ağacı PR'ın ağacına EŞİT olur, yani yeniden
kullanma *güvenilir* hâle gelir. Reuse iyileştirmesi zaten `strict`i
varsayıyordu — o varsayım yazılı değildi ve tutmuyordu; artık ikisi de doğru.

Doğrulandı, ayarın açık görünmesiyle yetinilmedi: main'in bir öncekine dayalı
bir deneme PR'ı GitHub tarafından `BEHIND` işaretlendi ve merge **reddedildi**
(*"the head branch is not up to date with the base branch"*), main kımıldamadı.
Deneme PR'ı yalnız-belge seçildi ki `detect-docs-only` derlemeleri atlasın.

> `strict`i kapatan, aynı anda main-push işindeki artefakt yeniden kullanımını
> da kapatmalı (testleri gerçekten koştursun) — yoksa yukarıdaki delik geri
> açılır. İkisinden biri olmalı.

Öteki iki koruma ayarı (inceleme zorunluluğu yok, `enforce_admins` kapalı)
**bilinçli tercih** — gerekçe ve yabancı birinin bunlardan yararlanamadığının
ölçümü: [[Decisions]].

## CI
`.github/workflows/build.yml` — Ubuntu + macOS + Windows. Linux: `./build.sh test`,
`./build.sh suites`, typeinfer koşucusu, SHA-256 yardımcısı. macOS (arm64): AOT dumanı +
`./build.sh suites` (AArch64 yolunun tek ölçüldüğü yer). Windows (MSYS2): `./build.sh
test`, `./build.sh suites`, typeinfer, DLL kapısı, kurulumcu. Hiçbiri `continue-on-error`
**değil**: bir test hatası CI'yı kırmızıya çeviriyor.

**Zaman aşımları (2026-10-02):** üç iş akışında her işin iş düzeyi, her adımın adım düzeyi
`timeout-minutes`'ı var (varsayılan 360 dk'ydı). Tetikleyen: Windows "Ornekler" adımı
2026-09-29'da iki kez 46–47 dk asılı kaldı — adımın 30 dk sınırı **işlemedi**, çünkü runner
"lost communication with the server" ile düştü (kaynak açlığı); adım `in_progress` kaldı ve işi
sunucu kalp atışı zaman aşımında bitirdi. Adım sınırı yalnız runner sağlamken işe yarar; Windows
Ornekler/Suitler ölçülen sürelere göre (son 40 başarılı koşum: en çok 65 / 200 sn) 10 / 12 dk'ya
indirildi. Runner kaybını sınırlar önleyemez — tekrarlarsa `TULPAR_TEST_JOBS` düşürülmeli.

## İlgili
[[Standard Library]] · [[Runtime]] · [[Cross-platform]] · [[AOT Backend]] · [[Testing]] · [[Tuzaklar]]
