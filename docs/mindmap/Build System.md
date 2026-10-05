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

## Derleme önbelleği — içerik adresli anahtar (2026-10-05)
`tulpar build` VE `tulpar dosya.tpr` aynı girdiyle ikinci kez derlemiyor
(`src/aot/aot_cache.{hpp,cpp}`). Anahtar, çıktıyı belirleyen her girdinin
SHA-256 özeti:

- sürücü: sürüm dizgisi + ikilinin KİMLİĞİ + yüklü `libLLVM` (dağıtım LLVM'i
  güncelleyince `tulpar` ikilisi aynı kalır ama kod üretimi değişir);
- ana kaynağın ve geçişli import'ların İÇERİĞİ — çözüm sırası kodgenle aynı
  (gömülü → eklenti → paket-yerel kardeş → literal → `.tpr` → `tulpar_modules/`),
  gömülü bir modülün diskten import ettiği dosya dahil (gömülü `router`
  `import "lib/http_utils.tpr"` yazıyor ve çalışma dizinine göre çözülüyor);
- `tulpar.toml`; eklentilerin bildirim + modül içeriği, arşiv kimlikleri;
- `libtulpar_runtime.a` / `libtulpar_tame.a` (+ OpenSSL `.a`) kimlikleri,
  link satırının arama dizinlerinin HEPSİNDE; bağlama sürücüsü (`TULPAR_CC`
  ya da varsayılan, PATH'te çözülmüş) ve `ld`;
- hedef üçlüsü + CPU, kip (çalıştır / derle + çıktı adı), dil, çalışma dizini;
- `src/aot/aot_cache_env.inc`'in DIŞLAMADIĞI her `TULPAR_*` ortam değişkeni
  (+ `LIBRARY_PATH`, `SDKROOT`, `MACOSX_DEPLOYMENT_TARGET` …).

"Kimlik" = boyut + mtime(ns) + ctime(ns) + inode (Windows: yazma zamanı +
NTFS ChangeTime + dosya kimliği): içerik okumadan değişimi görür; ctime geri
alınamaz, yani "içeriği değiştirip mtime'ı geri almak" da ıska verir.
Kaynaklar ve modüller İÇERİKLE özetlenir: `touch` tek başına ıska vermez.

**Ortam değişkenleri — güvenli yön.** Derleyicinin gördüğü her `TULPAR_*`
anahtarda; dışarıda kalabilen yalnız üç sınıf (`aot_cache_env.inc`):
ÇALIŞMA_ZAMANI (programın/düzeneğin okudukları: `TULPAR_ENGINE_*`, `*_TANI`,
`TULPAR_TEST_JOBS`, `TULPAR_NO_F64` …), DENETİM (`TULPAR_AOT_NOCACHE`,
`TULPAR_CACHE_*`) ve GÖZLEM (`TULPAR_AOT_EMIT_LL`, `TULPAR_AOT_TIME`,
`TULPAR_DBG_VER` … — biri ayarlıysa önbellek HİÇ kullanılmaz: isabet,
gözlemlenmek istenen derlemeyi atlardı). Fazla katmak yalnız gereksiz ıska
üretir; eksik katmak bayat ikili. Kapı `tests/onbellek_anahtari_kapisi.py`:
ÇALIŞMA_ZAMANI sınıfından bir ad derleyici tarafında (`src/aot`,
`src/typeinfer`, `src/parser`, `src/lexer`, `src/ext`, `src/main.cpp`) dizgi
sabiti olarak geçerse kırmızı; pozitif kontrolü (yapay ihlal ağacı) her koşumda.

**Öz denetim.** Kodgenin derleme sırasında GERÇEKTEN okuduğu her dosya
(`import_load_module` + ayrıştırıcının enum ön taraması, `note_input`)
anahtarın kapsadıkları arasında olmalı; değilse sonuç önbelleğe YAZILMAZ.
Çözüm kuralı kodgende değişip önbellekte unutulursa bedel bayat ikili değil,
ıska. Pozitif kontrol: `TULPAR_CACHE_SINAMA=tarama-yok`.

**Nerede.** Kök: `TULPAR_CACHE_DIR` > `$XDG_CACHE_HOME/tulpar` |
`~/.cache/tulpar` (macOS `~/Library/Caches/tulpar`, Windows
`%LOCALAPPDATA%\tulpar\cache`).
- `run/<anahtar>[.exe]`: `tulpar dosya.tpr` ikilileri; isabette DOĞRUDAN
  oradan çalışır (kopya yok). Yazma: geçici ad → asla-üzerine-yazmayan yayım
  (POSIX `link()`, Windows bayraksız `MoveFileEx`) — koşan bir `.exe`nin ya da
  yarışı kazanan sürecin ikilisinin üstüne yazılmaz; N süreç aynı anda
  derlerse kazananınki kullanılır. Tavan 512 MB (`TULPAR_CACHE_MAX_MB`), LRU
  (isabet mtime'ı tazeler; son 60 s içinde kullanılan silinmez).
- `build/<özet(çıktı yolu)>`: `tulpar build` kaydı — anahtar + çıktı
  ikilisinin KİMLİĞİ. İkili kullanıcının dizininde kalır ve ona hiçbir şey
  eklenmez/yazılmaz (kullanıcı dizini kirlenmez; Windows `.exe`; macOS imzası
  ve [[Tuzaklar#7l]]). İkili başka bir şeyle değişirse (debug derlemesi, `cp`,
  `strip`) kimlik tutmaz → ıska.
- Derlemenin stderr'i (kodgen uyarıları) yakalanıp girdiyle saklanır ve
  isabette AYNEN yeniden basılır; `[typecheck]` ön geçişi her koşuda canlı
  çalışıyor (~1–4 ms).

**Önbellek dışı:** web/Android (çıktı `output_name`'in kendisi değil: `.html`
üçlüsü / `<out>_apk` dizini; bugünkü kural korundu), `--debug` (optimizasyonsuz
ve hızlı, DWARF mutlak yol taşır), `--sanitize` (tanı amaçlı) — bunlar
derlenince eski kayıt silinir. Kapatma `TULPAR_AOT_NOCACHE=1`; temizlik
`tulpar cache clean`; durum `tulpar cache info`; kararlar
`TULPAR_CACHE_RAPOR=1` (`=2` anahtarın düz metni). Davranış kapısı
`tests/onbellek.sh` (`build.sh suites`).

Ölçüldü (2026-10-05, Ryzen 7 9800X3D, Linux, v3.39.6 tabanı, 7–9 koşu medyanı):

| | önce (her koşu derler) | ilk koşu (ıska) | isabet |
|---|---|---|---|
| `tulpar wings_groups_test` (serve'süz) | 576 ms | 575 ms | 6,9 ms |
| `tulpar nbody.tpr` (programın kendisi 39,2 ms) | 448 ms | 449 ms | 43,9 ms |
| `tulpar hello.tpr` | 48,2 ms | 48,7 ms | 4,4 ms |
| `tulpar build wings_groups_test` | 627 ms; eski isabet 8,0 ms (ikiliyi iki kez okuyordu) | 639 ms | 6,8 ms |
| `tulpar build nbody` | 400 ms | 413 ms | 3,9 ms |

"önce" ve "ilk koşu" sütunları turla eşlenmiş (eski sürücü / yeni sürücü
boş önbellekle sırayla, [[Tuzaklar#1n]]): ıska yolunun ek maliyeti gürültünün
içinde. Taban `tulpar --version` 3,6 ms (dinamik `libLLVM` yüklemesi);
isabetin geri kalanı `[typecheck]` ön geçişi (~1–3 ms) + anahtar (~0,1 ms).
`benchmarks/fair`'in 13 çekirdeğinde IR ve ikili eski sürücüyle bayt bayt
AYNI (önbellek yalnız derleme adımını atlıyor). → [[Tuzaklar#7n]]

> ⚠️ **Eski mtime önbelleği (2026-09-01 → 2026-10-05) girdilerin yarısını
> görmüyordu.** Çıktı kaynaktan, yerel import'lardan, eklenti dosyalarından ve
> sürücüden yeniyse "Cache hit" diyordu; kodgen ortam değişkenlerini, runtime
> arşivini, paket-yerel kardeş import'ları (`tulpar_modules/<p>/<ic>.tpr`) ve
> mtime'ı geri alınmış içeriği görmüyordu. Ölçüldü: `TULPAR_NO_FVER=1` ile
> ikinci `build` "Cache hit" deyip float sürümlü ESKİ ikiliyi bıraktı (ikili
> özeti `fc76f8ac…` iki kipte de; taze `TULPAR_NO_FVER=1` derlemesi
> `24dabaa2…`). Daha önce (2026-09-01'e kadar) import'lar hiç sayılmıyordu.

## Sürüm dizgisi git etiketinden, her derlemede (2026-10-02)
Eskiden `project(TulparLang VERSION 3.13.1)` + CMake **cache** değişkeni:
(1) elle yükseltilmesi gerekiyordu ve otomatik sürüm (her birleşmede etiket)
başladıktan sonra yükselten olmadı — dal/yerel derlemeler v3.37.x çıkmışken
`3.13.1-dev` diyordu; (2) önbellekliydi, yükseltilse bile aynı build
dizininde eski kalıyordu (ölçüldü 2026-09-02: 3.13.1'e yükseltildikten sonra
ikili `3.13.0-dev`).

Şimdi sayı **yok**: `cmake/TulparVersion.cmake` her `cmake --build`'de
(`tulpar_surum` hedefi) `git describe --tags --match 'v[0-9]*' --dirty`
üretir (`v3.37.16`, `v3.37.16-4-gabc1234[-dirty]`); etiket derlemesinde
`-DTULPAR_VERSION=<etiket>` aynen; git/etiket yoksa `0.0.0-dev[+g<sha>]` —
bilinmeyeni uydurmaz. Değişmeyen dizgi başlığa dokunmaz (yeniden derleme
yok). Eski build dizinlerinin önbelleğindeki `x.y.z-dev` değeri override
sayılmaz. CI checkout'u tam geçmişle (`fetch-depth: 0`, blobsuz) ve
`tools/surum_denetle.sh` üç işte derlemenin hemen ardından ikiliyi
etiketle/describe ile karşılaştırır (öz-denetimli). MSYS2'ye `git` paketi
bunun için eklendi (minimal PATH'te Windows git'i yok).

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

**Dinamik bağ kapısı (2026-10-02):** `tools/dinamik_bag_denetle.sh`, Linux ve macOS işlerinde
"Prepare artifact"tan hemen sonra. macOS: `otool -L`'deki her bağımlılık `/usr/lib` ya da
`/System` altında; `DYLD_PRINT_LIBRARIES` ile `--version` koşarken `/opt/homebrew` ya da
`/usr/local`'dan hiçbir şey yüklenmiyor; Homebrew LLVM Cellar'ı taşınmış ve `PATH` yalnız sistem
dizinleriyken `--version` + `tests/aot_smoke.sh` geçiyor. Linux: `NEEDED` listesi glibc,
libstdc++/libgcc_s, zlib, zstd, tinfo, OpenSSL 3 ile sınırlı (v3.38.0'da ölçülen durum; LLVM
statik, 68 MB), `/usr`/`/lib` dışı `RUNPATH` yok. Pozitif kontrol `--oz-sinama`: geçici dizindeki
bir dylib/.so'ya bağlı program kırmızı, sade program yeşil. Yerelde Arch/CachyOS derlemesi
(paylaşımlı `libLLVM.so`) bu kapıdan **bilerek** geçmez — kapı yayın ikilisi içindir.
Tetikleyen: v3.38.0 macOS ikilisi dört Homebrew dylib'ine bağlıydı ([[Cross-platform]]).

**Kullanıcı ikilisi kapısı (2026-10-05):** `tests/kullanici_ikili_bag.sh` — "sürücü
taşınabilir" ile "sürücünün ürettiği taşınabilir" ayrı iddialar. `tulpar build` ile
`tls_init(sertifika, anahtar)` çağıran bir program (ağ yok; sertifika `openssl req` ile
yerinde) linklenir, koşar (`tls:var` — TLS gerçekten içinde) ve ÇIKTISI
`dinamik_bag_denetle.sh`ten geçer; pozitif kontrol `TULPAR_AOT_LINK_FLAGS` ile geçici
dizindeki kitaplığa bağlanan ikilinin kırmızı görülmesi. macOS'ta iki kez: Homebrew
görünürken (ld64 aynı dizinde `.dylib`i `.a`ya tercih eder — asıl tuzak) ve LLVM **+**
openssl@3 Cellar'ları gizliyken. Düzeltme: macOS'ta OpenSSL statik arşivleri
`libtulpar_runtime.a`nın içine `libtool -static` ile katılır (CMake `TULPAR_TLS_IN_RUNTIME`),
sürücü `-lssl/-lcrypto` ve OpenSSL `-L` yazmaz (`aot_pipeline.cpp`). Düzeltmesiz sürücüyle
kapı iki ayakta da kırmızıydı (libssl.3.dylib bağı; gizliyken `ld: library 'ssl' not found`).
Bedel (macOS CI, 2026-10-05): arşiv 2,7 → 12 MB (19 → 1139 üye), TLS programı
450 432 → 5 585 296 bayt — yalnız TLS'ye dokunan programda; sade `print` programı 390 168 bayt
(`-dead_strip` ile 390 120, kazanç yok). İkisi de kapıda her koşumda basılır.
Aynı kapı Homebrew clang'ın libunwind bağını da yakaladı (PATH'teki `clang++`
`brew link llvm@18` sonrası Homebrew'unki): macOS'ta varsayılan link sürücüsü
`/usr/bin/clang++`, `TULPAR_CC` ezer.

**macOS TLS programı küçültme (2026-10-05, PR #465):** yukarıdaki "`-dead_strip` kazanç yok"
ölçümünün sebebi `-rdynamic`: ld64'te `-export_dynamic` demek ve çalıştırılabilirde her
global sembolü dead-strip kökü yapıyor. İki adım birlikte:
(1) link satırı `-rdynamic -Wl,-dead_strip -Wl,-hidden-ltulpar_runtime` (Linux'taki
`--exclude-libs,ALL` + `--gc-sections` karşılığı; kullanıcı sembolleri `t_<ad>` açık kalır,
`call()` yalnız onları dlsym'liyor); (2) OpenSSL macOS yayın CI'ında kaynaktan, `no-*` ile
(`tools/openssl_kucuk_derle.sh`: legacy/engine/GOST/SM*/QUIC/CMS/… kapalı, sürüm her koşumda
Homebrew openssl@3'ten okunur, tarball `.sha256` ile doğrulanır, `--openssldir` Homebrew'unkiyle
aynı). Ölçüm (macOS CI arm64, OpenSSL 3.6.4, `tools/tls_boyut_olc.sh`; bayt):

| kip | TLS programı | `strip -x` | sade `print` |
|---|---|---|---|
| eski satır, Homebrew OpenSSL | 5 585 296 | 5 215 784 | 390 168 |
| + yalnız `-dead_strip` | 5 585 040 | 5 215 784 | — |
| + yalnız gizli runtime | 5 362 048 | 4 579 552 | — |
| dead_strip + gizli (1) | 4 451 776 (-%20) | 3 886 232 | 50 616 (-%87) |
| eski satır, küçük OpenSSL (2) | 4 199 584 (-%25) | 3 878 264 | — |
| **(1) + (2) — yeni varsayılan** | **3 190 480 (-%43)** | 2 726 360 | 50 616 |

Bedel: küçük OpenSSL soğuk derleme 57 s (indirme+doğrulama 2 s, Configure+make+install 55 s,
`-j3`, macos-latest); sürüm + betik özeti anahtarıyla önbelleklenir. Bileşim (link haritası,
eski satır): libcrypto %61, runtime %27, libssl %12. Eklenmeyen: `-Wl,-x` (2,73 MB'a indiriyor
ama çökme raporunda runtime fonksiyon adlarını siliyor; Linux da sembol silmiyor) ve
kullanılmayan arşiv üyelerini ayıklamak (ld64 zaten yalnız başvurulan üyeleri çekiyor —
program boyutuna etkisi sıfır, yalnız arşiv küçülür). Kapı: `kullanici_ikili_bag.sh` macOS'ta
sade programın 150 KB altında kaldığını denetler (eski satır 390 168); CI, CMake'in küçük
OpenSSL'i seçtiğini `CMakeCache.txt`ten denetler.

**LLVM 18 kilidi (2026-10-05):** Linux işi apt ile `llvm-18-dev` kurar ama
`find_package(LLVM)` ipucu verilmeyince koşucu görüntüsündeki llvm-17'yi seçiyordu —
yayınlanan ikilinin RUNPATH'i `/usr/lib/llvm-17/lib` idi (2026-10-02, `llvm-readelf -d`).
"CI 18 ile derler" belgesi aylarca yanlıştı. `-DLLVM_DIR=$(llvm-config-18 --cmakedir)`.

**Kopya yeni inode'a (2026-10-05):** `build.yml`/`build.sh`teki her `cp build/tulpar tulpar`
önce hedefi siler. macOS'ta çalıştırılmış ikilinin yerinde üzerine yazılması belirlenimsiz
`Killed: 9` üretir ([[Tuzaklar]] 7l); macOS işindeki "Yerinde kopya olcumu" adımı
mekanizmayı `stat -f %i` ile kanıtlar.

**Zaman aşımları (2026-10-02):** üç iş akışında her işin iş düzeyi, her adımın adım düzeyi
`timeout-minutes`'ı var (varsayılan 360 dk'ydı). Tetikleyen: Windows "Ornekler" adımı
2026-09-29'da iki kez 46–47 dk asılı kaldı — adımın 30 dk sınırı **işlemedi**, çünkü runner
"lost communication with the server" ile düştü (kaynak açlığı); adım `in_progress` kaldı ve işi
sunucu kalp atışı zaman aşımında bitirdi. Adım sınırı yalnız runner sağlamken işe yarar; Windows
Ornekler/Suitler ölçülen sürelere göre (son 40 başarılı koşum: en çok 65 / 200 sn) 10 / 12 dk'ya
indirildi. Runner kaybını sınırlar önleyemez — tekrarlarsa `TULPAR_TEST_JOBS` düşürülmeli.

## İlgili
[[Standard Library]] · [[Runtime]] · [[Cross-platform]] · [[AOT Backend]] · [[Testing]] · [[Tuzaklar]]
