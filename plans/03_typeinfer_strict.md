# Plan 03 — typeinfer Strict Mode

**Durum:** COMPLETED (2026-05, PR #43 + PR #32 pre-pass çerçevesi) —
`tulpar typecheck` standalone subcommand + her `build`/`run`/`--vm`
çağrısında `[typecheck]` uyarıları (`--no-typecheck`/
`TULPAR_NO_TYPECHECK=1` opt-out). Bkz. STATUS § "Çekirdek dil".
**Tahmin:** 1-2 PR (false-positive cleanup ayrı PR)
**Risk:** Düşük (mevcut altyapı %90 hazır, opt-in flag)
**Mottoya katkı:** Python kolay (erken hata, runtime'a kalmadan)

## Hedef

Şu an `tulpar` / `tulpar build` / `tulpar --vm` komutları typeinfer'ı
**warning mode**'da çalıştırıyor (`[typecheck] path: msg` stderr'a
basılıyor, exit code etkilenmiyor). Standalone `tulpar typecheck`
subcommand'i **error mode**'da çalışıp exit 1 veriyor. Bu plan
**`--strict` flag'i** + **`TULPAR_STRICT=1` env** + **`tulpar.toml`
`strict = true`** üçlüsünü ekleyerek warning'leri exit-blocking error'a
çevirecek opt-in bir mod sunuyor; aynı zamanda yanlış-pozitif kaynaklar
temizleniyor.

## Mevcut durum (kaynak)

### Mimari — single shared core, two entry points

- [src/typeinfer/typeinfer.cpp](../src/typeinfer/typeinfer.cpp)
  (~1000 satır) çekirdek inference; `TypeInferContext.warning_mode`
  bool flag'i error vs warning davranışını kontrol ediyor.
- [src/typeinfer/typeinfer_warn.cpp:23-58](../src/typeinfer/typeinfer_warn.cpp#L23-L58)
  `typeinfer_emit_warnings()` — warning_mode=true, format
  `"[typecheck] %s: %s\n"`, exit code'u etkilemez.
- [src/cli/typecheck_cmd.cpp:25-93](../src/cli/typecheck_cmd.cpp#L25-L93)
  `typecheck_cmd_main()` — warning_mode=false (default), format
  `"Type Error: %s\n"`, errors > 0 ise return 1.
- [src/typeinfer/typeinfer.cpp:33](../src/typeinfer/typeinfer.cpp#L33)
  Prefix string: `"[typecheck] %s: %s\n"`.

### CLI integration

- [src/main.cpp:541](../src/main.cpp#L541) `int skip_typecheck = 0;`
- [src/main.cpp:550](../src/main.cpp#L550) `--no-typecheck` parse
- [src/typeinfer/typeinfer_warn.cpp:18-20](../src/typeinfer/typeinfer_warn.cpp#L18-L20)
  `TULPAR_NO_TYPECHECK` env var.

### Yanlış-pozitif kaynakları (`is_unknown` masked)

[src/typeinfer/typeinfer.cpp:299-308](../src/typeinfer/typeinfer.cpp#L299-L308):

```cpp
auto is_unknown = [](DataType t) {
  return t == TYPE_VOID || t == TYPE_UNKNOWN || t == TYPE_CUSTOM;
};
```

Şu an conservative — bu üç kategori type-mismatch check'ten muaf
tutuluyor. Strict mode'da naif bir flag flip mevcut programları çoklu
**false positive** ile patlatır:

1. **TYPE_CUSTOM:** Kullanıcının tanımladığı struct (Plan 04 ile gelecek
   ya da mevcut `TypeDecl` yapısı). Field tipleri tracked olmadığı için
   `int x = p.field;` mismatch sanıyor.
2. **TYPE_VOID:** Builtin signature catalog'da olmayan bir builtin'in
   return type'ı. `len`/`abs` çözüldü ama 50+ builtin'in bir kısmı hâlâ
   katalog dışı (variadik `print`/`println`, `call`, vb.) — bunlar
   bilerek, ama yanlış işaretlenmemeli.
3. **TYPE_UNKNOWN:** `var x = ...` declaration'ı. Spec'te
   "kullanıcı tipini explicitly ' var' yazdı" → strict'te de OK olmalı.
4. **JSON field access:** `arr["key"]` her zaman `TYPE_VOID` infer
   ediyor ([typeinfer.cpp:266-282](../src/typeinfer/typeinfer.cpp#L266-L282)).
   Strict'te `int y = arr["key"];` ya OK kalmalı (bilinçli) ya da
   "kategorical OK" olarak işaretlenmeli — kategori farkı önemli.

### Test coverage

`tests/typeinfer*` **yok**. Şu an typeinfer'a doğrudan unit-test yok;
EKSIKLER #50 (`len`/`abs` polimorfizm) bile manuel example tarama ile
doğrulanmış.

## Yaklaşım: İki PR

### PR 1 — False-positive temizliği (gerek koşulu)

**Hedef:** Naif `--strict` flag flip ettiğimizde mevcut
`examples/*.tpr` ve `lib/*.tpr` setinin **0 hata** üretmesi. Bu PR
behavior değişikliği değil, sadece doğruluk:

1. **Builtin catalog genişletme:** Variadik (`print`, `println`) için
   "skip mismatch check" yerine "varargs" kategorisi tanımla — arity
   her zaman pass eder, type pass eder ama TYPE_VOID dönmez (return
   void). Cataloga koy.
2. **JSON `arr[i]`/`obj["k"]` access:** TYPE_VOID yerine yeni
   `TYPE_DYNAMIC` (veya zaten var olan TYPE_UNKNOWN). Anlam:
   "compile-time'da bilinemiyor, runtime check'e bırakılıyor".
   `is_unknown` listesine TYPE_DYNAMIC zaten girer. Strict'te bile
   yumuşak.
3. **`var` declaration:** TYPE_UNKNOWN zaten muaf. Doğru davranış.
4. **TYPE_CUSTOM:** Plan 04 ile field tracking gelene kadar muaf. Plan
   04 entegre olunca bu muafiyet kalkacak.

**Doğrulama:** `examples/` ve `lib/` üzerinde
`TULPAR_STRICT=1 tulpar typecheck <file>` her dosyada exit 0.

### PR 2 — `--strict` flag entegrasyonu

**Yeni dosya/değişiklik yok, sadece flag plumbing:**

1. [src/main.cpp](../src/main.cpp): `--strict` arg parse, `int strict = 0;`
   değişkeni. `TULPAR_STRICT` env var oku.
2. [src/typeinfer/typeinfer_warn.cpp](../src/typeinfer/typeinfer_warn.cpp):
   `typeinfer_emit_warnings_with_mode(source, path, strict_mode)`
   varyantı ekle. Strict modda warning_mode=false, return error count.
3. main.cpp run/build path'i:

   ```c
   int errcount = typeinfer_emit_warnings(source, path, strict);
   if (strict && errcount > 0) {
     free(source);
     return 1;
   }
   ```

4. [src/pkg/manifest.cpp](../src/pkg/manifest.cpp): `tulpar.toml`
   top-level keys'e `strict = true` desteği. Manifest struct'a `bool
   strict_typecheck` ekle. Driver project root'taki manifest'i okusa,
   flag'i set etsin (precedence: CLI > env > manifest > default false).
5. CLI help update + README'de `--strict` bahsi.

**Format farkı (önemli):** Warning mode'daki `[typecheck] path: msg`
prefix korunmalı (kullanıcı script'i bu satırı parse ediyor olabilir).
Strict mode'da extra satır eklenmeli — örn:

```
[typecheck] examples/foo.tpr:23: expected int, got str
[typecheck] examples/foo.tpr:31: ...
[typecheck] 2 errors (strict mode); aborting build.
```

Exit 1.

### PR 3 (opsiyonel) — typeinfer test suite

`tests/typeinfer/` dizini, `lib/test.tpr` üzerinden değil ama özel
"compile-and-grep-stderr" runner ile (tulpar typecheck stderr çıktısını
beklenen string ile karşılaştır):

```
tests/typeinfer/
  pass/
    01_int_assignment.tpr        # exit 0
    02_polymorphic_len.tpr       # exit 0
  fail/
    01_str_to_int.tpr            # exit 1, stderr contains "expected int"
    02_arity_mismatch.tpr        # exit 1, stderr contains "expected 2 args"
```

Custom runner olarak `tests/typeinfer/run.ps1` veya `run.sh`. Ya da
`run_tests.ps1` içine yeni bir kategori. Bu ayrı bir PR — typeinfer'ı
birden fazla yere açacaksak (örn. LSP) regresyon koruması için
kritik.

## Edge case'ler

1. **`--strict` ile `--no-typecheck` çakışması:**
   Net karar: `--no-typecheck` her şeyi kapatır (öncelik). `--strict
   --no-typecheck` çelişkili kombinasyon → uyarı verip continue.
2. **Lock dosyası (Plan 02 ile):** `tulpar.lock` da strict mode'dan
   etkilenmez — lock değişmez.
3. **`tulpar fmt`:** typeinfer'a girmeyen subkomutlar etkilenmez. fmt
   her durumda continue.
4. **LSP integration:** LSP shouldcheck on save → strict mode'u
   `tulpar.toml`'a göre yansıtsın, CLI flag'i değil. Bu plana özel iş
   değil; LSP yan etkisi olarak ileride doğru davranır.
5. **Build cache:** typeinfer çıktısı caching yapısında değil; mod
   değişikliği cache'i invalidate etmez ama typeinfer her zaman
   tekrar çalışır. Bu zaten halihazırdaki davranış.

## Doğrulama

- `examples/*.tpr` üzerinde `TULPAR_STRICT=1 tulpar typecheck` exit 0
  (PR 1 sonrası).
- `tests/typeinfer/fail/*.tpr` için exit 1 + beklenen stderr (PR 3
  sonrası).
- CI workflow'unda yeni bir job: `typeinfer-strict` — Linux'ta
  `for f in examples/*.tpr; do TULPAR_STRICT=1 ./tulpar typecheck "$f"; done`
  toplam exit 0.
- Mevcut `./build.sh test` davranışı değişmez (strict default kapalı).

## Açık sorular

- **`--strict` precedence:** CLI > env > manifest. Doğru mu? Önerim:
  evet (en spesifik en geçerli).
- **`var` keyword'ü stricter olabilir mi?** Şu an `var x = "a"; x = 1;`
  sessiz kabul ediliyor (TYPE_UNKNOWN). Strict mode'da first-write tipi
  kilitlenip ikinci atamada hata mı? Faz 2'ye bırak — geriye
  uyumsuzluk riski var.
- **JSON `arr["key"]`'ın kategori muafiyeti:** Strict'te bile
  `int y = arr["key"];` OK mu, yoksa `var y = arr["key"];` zorunlu mu?
  Önerim: ilk versiyon OK (geriye uyumlu), ikinci faz "must use var" —
  kullanıcı tepki verirse geri al.
- **CI'da default açalım mı?** İlk merge sonrası birkaç hafta opt-in
  bırakıp ekosistemi büyütüp `examples/*.tpr` dışında gerçek paketler
  üzerinde test ettikten sonra "warning mode'u sessiz kapat, strict
  default'a yap" tercihi açılabilir. Zorla flip etme.
