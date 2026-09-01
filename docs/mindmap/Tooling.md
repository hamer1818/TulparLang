---
tags: [subsystem, tooling]
---

# Tooling — fmt / pkg / update

CLI dispatch `src/main.cpp`'de; `--lsp`, `fmt`, `pkg`, `version`, `--help`, `update` run/build yolundan önce short-circuit eder.

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
`src/pkg/` — `tulpar pkg <init|add|install|list|remove|search>`. `manifest.cpp` `tulpar.toml` okur; `pkg_cli.cpp` `path:` bağımlılıkları `tulpar_modules/<name>/`'e vendor'lar, `url:` tek dosya çeker, gerisi registry'den (`fetch_versions` + indirme — "registry TODO" notu BAYATTI). Bağımlılık sözdizimi bir DİZGİ: `mathx = "path:../dir"`; inline tablo (`{ path = ... }`) desteklenmiyor ve net hata veriyor. Denetim: `tests/pkg_audit.sh` (`build.sh suites`) — init/add/install zinciri, vendor edilenin GERÇEKTEN import edilebilmesi, ve hata yollarının sıfırdan farklı dönmesi. → [[Imports and Modules]]

## Type checker — `tulpar typecheck`
`src/cli/typecheck_cmd.cpp` — ön-geçişteki (`[typecheck]` uyarıları) aynı denetleyici,
**hata kipinde**. Her `tulpar`/`tulpar build` çağrısında zaten koşuyor;
`--no-typecheck` / `TULPAR_NO_TYPECHECK=1` kapatır, `--strict` uyarıları sert hataya
çevirir. → [[Type Inference]]

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
`src/cli/debug_cmd.cpp` — **deneysel** DAP adaptörü. Denetimi yok.

## Self-update
`src/cli/update_cmd.cpp` — `tulpar update [--check]`, tulparlang.dev'den. Denetimi yok.

## Denetim durumu
`./build.sh suites` fmt · doc · pkg · LSP · dist arşiv · builtin · kod üretimi
denetimlerini koşuyor. **Denetimsiz kalan:** `tulpar debug`, `tulpar update`.
Yayınlanan araçların sessizce çürüdüğü bir tur yaşandı — fmt derlenmeyen kod üretiyordu,
doc üç stdlib modülünü belgeleyemiyordu, ikisi de bütün testler yeşilken.
→ [[Tuzaklar]] §6c

## İlgili
[[LSP]] · [[Imports and Modules]] · [[Build System]] · [[Type Inference]] · [[Testing]] · [[Tuzaklar]]
