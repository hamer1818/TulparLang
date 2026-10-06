---
tags: [subsystem, tooling]
---

# Tooling — fmt / pkg / update / cache

CLI dispatch `src/main.cpp`'de; `--lsp`, `fmt`, `pkg`, `version`, `--help`, `update`, `cache` run/build yolundan önce short-circuit eder.

## Derleme önbelleği — `tulpar cache`
`tulpar cache [info|clean|dir]` (`src/aot/aot_cache.cpp` `cache_cmd_main`):
`info` kök dizini, çalıştırma ikililerinin sayısını/boyutunu, tavanı, `build`
kayıtlarını ve önbelleğin açık mı (değilse neden) olduğunu yazar; `clean`
yalnız ÖNBELLEĞİN ADLANDIRDIĞI dosyaları siler (`<anahtar>[.exe]`,
`<anahtar>.diag`, `tmp-*`, kayıtlar — dizine başka bir şey konmuşsa
dokunmaz); `dir` kökü basar. Eklentiler yüklenmeden ÖNCE dağıtılır: bozuk bir
`TULPAR_EXT_PATH` temizliği engellemesin. Kapatma `TULPAR_AOT_NOCACHE=1`;
kararlar `TULPAR_CACHE_RAPOR=1` (`[onbellek] isabet/iska/kapali ...` stderr'e),
`=2` anahtarın düz metni (iki koşunun metnini karşılaştırmak ıskanın nedenini
söyler). Model, anahtar, saklama yeri ve gerekçeleri: [[Build System]].

## Formatter
`src/fmt/` — `tulpar fmt script.tpr`. Denetim: `tests/fmt_audit.sh` (`build.sh suites`) —
depodaki her `.tpr` üzerinde **idempotent** (iki kez biçimlendirmek aynı sonucu verir)
ve **hâlâ ayrışıyor** (biçimlendirilen dosya derlenebiliyor).

> ⚠️ **DERLENMEYEN kod üretiyordu** — üç ayrı bozulma, hepsi sessiz:
> `i++` → `i + +` (84 dosya `++` kullanıyor), `=>` → `= >`, ve blok yorumlarının
> içi kod sanılıp yeniden biçimlendiriliyordu. Bir biçimlendiricinin çıktısının
> derlendiğini kimse ölçmüyordu; testler yeşilken araç kırıktı. Çare: `++`/`--`
> bitişik tek belirteç, `two('=','>')`, ve blok yorumu satırlarının **aynen**
> kopyalanması (`line_opens_block_comment` / `line_closes_block_comment`).

## Package Manager
`src/pkg/` — `tulpar pkg <init|add|install|list|remove|search>`. `manifest.cpp` `tulpar.toml` okur; `pkg_cli.cpp` `path:` bağımlılıkları `tulpar_modules/<name>/`'e vendor'lar, `url:` tek dosya çeker, gerisi registry'den (`fetch_versions` + indirme — "registry TODO" notu BAYATTI). Bağımlılık sözdizimi bir DİZGİ: `mathx = "path:../dir"`; inline tablo (`{ path = ... }`) desteklenmiyor ve net hata veriyor. Denetim: `tests/pkg_audit.sh` (`build.sh suites`) — init/add/install zinciri, vendor edilenin GERÇEKTEN import edilebilmesi, ve hata yollarının sıfırdan farklı dönmesi. Registry yolu: `tests/pkg_registry_audit.py` — yerel sahte registry, her adımda İSTEK GÜNLÜĞÜ sayılıyor (aralık+lock+sha256, `.tpkg` kardeş import, önbellek, `--update`, registry kapalı, bozulan dosya, publish şekli). → [[Imports and Modules]]

> ⚠️ **Lock sürümü SABİTLEMİYORDU, `.tpkg` önbelleğe hiç girmiyordu** (2026-09-27'de
> düzeltildi). Aralık (`^1`) her install'da registry'ye karşı yeniden çözülüyordu:
> yeni sürüm yayınlanınca sessizce yükseliyor, registry kapalıyken kilitli ve diskte
> duran bağımlılık bile düşüyordu. `.tpkg`'de lock ARŞİVİN özetini tutuyor, önbellek
> denetimi açılmış `<ad>.tpr`'yi özetliyordu — hiç tutmadı, her install yeniden
> indirdi. Artık kilitli sürüm aralığı karşıladıkça kullanılıyor (`--update`
> yeniden çözer) ve arşiv `tulpar_modules/<ad>/.<ad>.tpkg` olarak saklanıyor;
> isabet için arşiv özeti + açılmış her dosyanın aynen durması gerekiyor. Açık:
> projeler arası global önbellek (`~/.cache/tulpar`) yok. **Aynalar** var (2026-09-28):
> `[registry] mirrors = [...]` okuma yedeği, lock hangi aynadan geldiğini kaydeder ve aynı
> ad@sürümün sha256'sı aynalar arasında da tutmalı; `publish` yalnız `url`'e.

## Analiz — `tulpar analyze` (K157, 2026-09-29)
`src/cli/analyze_cmd.cpp`: derlemeden/linklemeden (i) dosyanın her üst düzey fonksiyonu için
`@no_alloc` kuralıyla ayırma raporu (typeinfer `typeinfer_alloc_report`, geçişli, beyaz liste —
"ayırmasız" kanıtlı, "AYIRIYOR" temkinli olabilir) ve (ii) `TULPAR_PERF_HINTS` hızlı yol ipuçları.
Kural kopyası yok. Kapı `tests/analyze_smoke.sh`: raporun her satırı `@no_alloc` + `typecheck` ile
aynı sonucu veriyor mu. Çalışma zamanında ölçülmedi. Metot çağrısı `p.f()` 2026-10-02'ye
kadar "beyaz listede olmayan yerleşik" sayılıyordu (K003 + K041); artık hedef kodgenle aynı
sırayla (`Tip.f(p)` → `m__f` → alıcının struct tipine göre `Tip.f` → serbest `f`) çözülüyor.
Alıcının tipi statik bilinmiyor ve bu adda bir yöntem varsa tanı bunu adıyla söylüyor
(`tests/typeinfer/{pass/24,fail/31}_no_alloc_yontem*.tpr`).

## Type checker — `tulpar typecheck`
`src/cli/typecheck_cmd.cpp` — ön-geçişteki (`[typecheck]` uyarıları) aynı denetleyici,
**hata kipinde**. Her `tulpar`/`tulpar build` çağrısında zaten koşuyor;
`--no-typecheck` / `TULPAR_NO_TYPECHECK=1` kapatır, `--strict` uyarıları sert hataya
çevirir. → [[Type Inference]]

> ⚠️ **Bayrak KONUMA göre sessizce düşüyordu** (2026-09-27'de düzeltildi).
> `tulpar build --strict x.tpr` çıkış 0 veriyor, `tulpar build --debug x.tpr`
> DWARF'sız ikili bırakıyordu — yalnız `tulpar --strict build` biçimi
> çalışıyordu; önbellek de debug/düz derlemeyi ayırt etmiyordu. Kapı:
> `tests/build_bayraklari.sh`. Ders: bir bayrağın "var" olması, her yazımda
> ETKİ ettiği anlamına gelmez; tanınmayan bayrak artık uyarı basıyor.

> ⚠️ **Ayrıştırma hatalarına KÖR'dü.** Ayrıştırıcı hatadan kurtuluyor: tanıyı basıp
> KISMİ bir AST döndürüyor, istisna atmıyor. Yalnız `catch` ve `!ast` denetimine güvenen
> komut, sözdizimi bozuk bir dosyaya **"ok" deyip çıkış kodu 0** dönüyordu — yani onu
> kapı olarak kullanan bir CI ayrıştırma hatalarını hiç görmüyordu. Artık
> `parser_get_error_count()` okunuyor.

## Belge üretici — `tulpar doc`
`src/cli/doc_cmd.cpp` — baştaki yorum bloklarından Markdown referans üretir.

> ⚠️ **Derleme başarısı, belge ön koşulu SANILIYORDU.** Kodgen hatası belgeyi
> engelliyordu; oysa belge **bildirimlerden** çıkıyor ve indeks kodgen'den bağımsız
> kuruluyor. Ölçüldü: `router` / `middleware` / `http_utils` kardeş modüllerin
> sembollerine baktıkları için TEK BAŞLARINA derlenmiyor — birlikte import edilince
> geçerliler. Üçü de belgelenemez durumdaydı ve komut hiçbir şey basmıyordu. Artık
> ayrıştırma hatası (belge çıkmaz, `1`) ile kodgen hatası (uyarı + belge basılır)
> ayrıldı. Denetim: `tests/doc_audit.sh`.

## Hata ayıklayıcı — `tulpar debug`
`src/cli/debug_cmd.cpp` — DAP adaptörü, gdb MI3 köprüsü. Denetim: `tests/dap_audit.py`
(`build.sh suites`, penceresiz stdin/stdout JSON): breakpoint durması + stackTrace +
yerel değerler, koşullu breakpoint (koşulsuz beş durma pozitif kontrolüyle), logpoint
(çıktı + devam, durma yok), `exited`+`terminated`, ve gdb yokken `launch`'ın kurulum
ipucuyla düşmesi. gdb yoksa (macOS CI) görünür atlanır; Linux CI'da
`TULPAR_DAP_ZORUNLU=1` atlamayı kırmızıya çeviriyor.

> ⚠️ **"Bütün handler'lar tamam" diyordu, HİÇBİR durma olayı üretmiyordu**
> (2026-09-27'de düzeltildi). Okuyucu iş parçacığı MI kaydını `'*'` önekiyle
> iletiyordu; `"stopped"` karşılaştırması depo geçmişinin başından beri hiç
> tutmadı. stackTrace/variables istekleri çalıştığı için elle sonda "çalışıyor"
> diyordu — ama VS Code'da breakpoint'te duraklama görünmüyor, oturum program
> bitince kapanmıyor, logpoint sessiz bir duraklamaya dönüşüyordu. Ders: istek
> ↔ cevap çalışıyor diye OLAY yolu çalışıyor sayılmaz; kapı olayları bekliyor.
> Ölçülmeyen: data/instruction breakpoint.

**Değerler okunur (2026-09-27).** Her Tulpar yereli DWARF'ta 128 bitlik opak `VMValue`
temel tipi; gdb/DAP `str ad = "Hamza"` için `130514698818214998946349060` basıyordu.
`tools/gdb/tulpar_printers.py` etiketi + yükü çözüyor (`"Hamza"`, `2.5`, `[1, 2, 3]`,
`{"k": 7}`, `true`); bağdaştırıcı onu ikiliye gömülü taşıyor ve gdb'yi başlatınca
yüklüyor, düz gdb kullanıcısı için `tulpar debug --gdb-script > t.py` + `source t.py`.
Nesne düzeni (`ObjString`/`ObjArray`/`ObjObject` ofsetleri) runtime DWARF'sız
linklendiği için betikte YAZILI — `vm.hpp`'de düzen değişirse `dap_audit.py`'nin
"okunur değerler" senaryosu kızarır.

**Struct'lar `print` ile aynı metin (2026-10-06).** Tipli struct dizisi
(`P[] pa`, `OBJ_STRUCT_ARRAY`) `<struct_array @0x…>` yerine `[P { x: 1, y: 2.5,
ok: true }]` — eleman alanları runtime'ın `sarr_field_value`'sunun Python kopyasıyla
(eski 8 B yuva düzeni + f32/i32/C bool C yerleşimi). Kutulu struct (ad etiketli json,
`Obj::struct_tag`) `{"x": 1}` yerine `P { x: 1 }`: ad tablosu runtime'da
`static g_struct_tag_names[256]`; runtime DWARF'sız ama sembol tablosunda, betik
adresini `&'g_struct_tag_names'` ile bir kez çözüyor (bulunamazsa — soyulmuş ikili —
json biçimine düşer). Kapı `dap_audit.py` (`pa`/`ta`/`bx`/`s`, DAP ve düz gdb);
eski yazıcıyla dördü de kırmızı.

## Self-update
`src/cli/update_cmd.cpp` — `tulpar update [--check] [--force]`, GitHub Releases'ten;
SHA-256 listesi indirilip her varlık doğrulanıyor, sonra kurulum dizinine yerleştiriliyor.

**En kırılgan adım indirme değil, YERLEŞTİRME.** Hazırlık dizini `$TMPDIR`
(varsayılan `/tmp`), hedef ise kurulum dizini — ve ikisi ayrı dosya
sistemlerinde olabiliyor. `rename(2)` o sınırı geçemez, `EXDEV` döner.
Ölçüldü 2026-09-22: `/tmp` tmpfs olan bir makinede komut, 68 MB'lık indirmeyi
ve doğrulamayı **başarıyla bitirdikten sonra** son adımda düşüyordu. Artık
sınır aşılırsa hedefin yanına kopyalanıp aynı dizin içinde yeniden
adlandırılıyor (atomik ve çalışan ikili üzerinde güvenli).

Kapısı: `tests/update_yerlestirme.sh`. Gerçek bir güncelleme ağ istediği için
yol **ağdan bağımsız** ölçülüyor — `tulpar update --test-install=<dizin>` yalnız
yerleştirmeyi koşturuyor ve hangi aygıtlar üzerinde çalıştığını basıyor, yani
deneme sınırı hiç geçmediyse bunu söylüyor.

## Denetim durumu
`./build.sh suites` fmt · doc · pkg · LSP · DAP · dist arşiv · builtin · argüman geçişi ·
güncelleme yerleştirmesi denetimlerini koşuyor. (`tulpar debug` 2026-09-27'ye kadar
denetimsizdi — ve kırıktı.)

Yayınlanan araçların sessizce çürüdüğü bir tur yaşandı — fmt derlenmeyen kod üretiyordu,
doc üç stdlib modülünü belgeleyemiyordu, ikisi de bütün testler yeşilken.
Aynı sınıftan ikinci vaka 2026-09-22'de çıktı: `tulpar update` bu satır
"denetimsiz" derken Linux'ta hiç çalışmaz halde yayınlanmıştı (`EXDEV`).
Denetimsiz bir yayınlanan araç, çalışmayan bir yayınlanan araçtır.
→ [[Tuzaklar]] §6c

## İlgili
[[LSP]] · [[Imports and Modules]] · [[Build System]] · [[Type Inference]] · [[Testing]] · [[Tuzaklar]]
