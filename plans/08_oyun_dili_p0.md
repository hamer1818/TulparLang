# Plan 08 — Oyun dili P0: `enum`, kutusuz float struct, çoklu dönüş

**Durum:** DİL TARAFI TAMAM (2026-09-21) — P0.2 `enum` ✅ (dal `dil/enum`), P0.3 float struct ✅
(dal `dil/struct-float-kutusuz`), P0.1 tuple ✅ (dal `dil/coklu-donus`). Kalan: motor deposunda
P0.4 (oyun yeniden yazımı).
**Tahmin:** 3 PR (enum → float struct → tuple), sonra motor deposunda 1 PR (oyun yeniden yazımı).
**Risk:** Orta — P0.2/P0.1 ayrıştırıcı şekeri (codegen'e dokunmaz); P0.3 codegen'in 20+ struct
yerleşim noktasına dokunur.
**Mottoya katkı:** Python kolay (enum, `a, b = f()`) + C kadar hızlı (float struct native,
tuple sıfır tahsis).

## Neden

Dört bağımsız değerlendirmenin sentezi (2026-09-21): Tulpar'ın oyun döngüsünde kullanılabilmesi
için önce **veri yerleşimi** (kutusuz float struct), sonra **ergonomi** (enum, çoklu dönüş),
sonra ABI/bellek (typed struct dizileri, f32, frame arena — P1+). Kanıt motor deposundaki gerçek
oyundan (tulpar-engine `tulpar/examples/engine_aksiyon.tpr`):

- "Tulpar'da çoklu dönüş yok: iki global" → `g_yon_x`, `g_yon_z`.
- "kutulu struct dizisi yok" → düşman verisi 11 paralel dizi.
- `int EKRAN_MENU = 0; …` sihirli sayılar; `g_ekran`/`g_durum` 39 karşılaştırma.

Kök neden: `struct_is_trivially_unboxable` (`src/aot/llvm_backend.cpp`) yalnız int/bool kabul
ediyor; bir `float` alan struct'ı string anahtarlı VM_OBJECT'e düşürüyor.

## P0.2 — `enum` ✅

```tpr
enum Ekran { MENU, OYUN, DURAKLAT = 5, AYAR }   // 0, 1, 5, 6 ; `sayım`/`sayim` TR
Ekran e = Ekran.MENU;                           // Ekran = int
match e { Ekran.MENU => ..., _ => ... }
```

Tasarım: tamamen **ayrıştırıcı şekeri**. `Ekran.MENU` `IntLiteral`'e katlanır, `Ekran` tip
adı `TYPE_INT`'e çözülür; codegen ve typeinfer anahtar kelimeyi görmez. Bildirim sırası
önemsiz: `parse()` başında token dizisi üstünde ön tarama (`prescan_enums`). Bildirim AST'de
`EnumDecl` / `AST_ENUM_DECL` olarak kalır (codegen no-op) — LSP üyeleri buradan indeksler.

Dosyalar: `src/lexer/{lexer.hpp,lexer.cpp}` (`TOKEN_ENUM`, sona eklendi), `src/parser/ast_nodes.hpp`
(`EnumDecl`), `ast_visitor.hpp`, `parser.hpp/.cpp` (`prescan_enums`, `parse_enum_decl`,
`parse_postfix` `.` katlaması, `parse_type`), `src/aot/llvm_backend.cpp` (`case AST_ENUM_DECL`),
`src/typeinfer/typeinfer.cpp` (erken dönüş), `src/lsp/{lsp_server,document_index}.cpp`.

Sınırlar (v1): yalnız üst düzey; aynı dosya (import edilen modülün enum'u ithal eden dosyada
görünmez); nominal değil (int ile karışır, `match` tamlık uyarısı yok). v2: typeinfer'da nominal
enum tipi + tamlık uyarısı; modül çözümlemesinin ayrıştırıcıya girmesi.

Doğrulama: `tests/enum.test.tpr` (7), `tests/enum_hatalari.sh` (6 hata yolu, `build.sh suites`
içinde), `examples/43_enum.tpr`, `tulpar fmt` idempotent.

## P0.3 — float alanlı struct kutusuz ✅

Yapıldı (2026-09-21): yardımcılar `struct_field_llvm_type` / `struct_field_load_boxed` /
`struct_field_payload_from_boxed` / `struct_field_store_from_boxed` (`llvm_backend.cpp`,
`struct_is_trivially_unboxable` yanında); 10 codegen noktası bunlara geçti; runtime
`aot_struct_unpack_typed` (alan tipli geri açma; eski `aot_struct_unpack_named` imzasıyla duruyor).
Ölçüm: `benchmarks/vec3_sum` **1671,8 → 49,5 ms** (C 6,5), int `struct_sum` 8,1 → 0,6 ms; IR:
`%tulpar_struct.Vec3 = type { double, double, double }`, sıcak fonksiyonda 0 tahsis çağrısı.
Yolda bulunan ve kapatılan üç eski hata (int struct'ta da vardı): bütün-struct yeniden atama
(`acc = f(acc, d)` → 0), bildirimde kopya (`Vec3 b = a;` → SIGSEGV), fonksiyondan struct global
erişimi (çalışma zamanı hatası). Bunlar P0.4'ün (oyun döngüsü) ve P0.1'in (tuple = sentezlenmiş
struct) ön koşuluydu.

Planlanan tasarım (uygulanan hâliyle):

Alan başına statik tipli 8 baytlık yuva: `int/bool → i64`, `float → double`. Yerleşim 8
bayt/alan kaldığı için `ObjStruct::fields[int64_t]` ile bit-kopya (`aot_struct_alloc_from_fields`,
`aot_struct_unpack_to`) değişmez; yükleme/saklama/kutulama noktaları alan tipine göre dallanır.
İç içe struct / str / dizi alanları P1'de.

Adımlar (`src/aot/llvm_backend.cpp`): `struct_is_trivially_unboxable` float kabul eder;
`register_struct_type` float → `float_type`; üç yardımcı (`struct_field_llvm_type`,
`struct_field_load_boxed`, `struct_field_store_from_boxed` — int→float `sitofp` zorlaması dahil)
ve tüm `st->llvm_type` noktaları bunlara geçer (`grep -n "st->llvm_type"`); `print(struct)` `%g`;
`match` destructure `aot_struct_get_field_ptr` sonucu double için `bitcast`. Runtime:
`aot_struct_unpack_named` `IS_FLOAT` dalı.

Doğrulama: `tests/struct_float.test.tpr`; mevcut `struct_nontrivial.test.tpr` ve
`examples/41_struct_entities.tpr` değişmeden yeşil; `benchmarks/vec3_sum` önce/sonra;
IR kanıtı `tulpar_struct.Vec3 = type { double, double, double }`.

## P0.1 — çoklu dönüş / tuple ✅

Yapıldı (2026-09-21) — aşağıdaki tasarım olduğu gibi uygulandı: `prescan_tuple_sigs` (token ön
taraması), `parse_tuple_type_list`, `tuple_struct_name` (sentez + kayıt; `parse()` sonunda TypeDecl'ler
programın başına), `parse_return_statement` Block açılımı, `parse_tuple_var_decl` /
`parse_tuple_assignment` + `pending_after_` (`parse_block`/`parse()` boşaltır, süslü parantezsiz
gövdeler `reject_pending` ile reddeder), lambda gövdesinde tuple bağlamı kapalı. Codegen/typeinfer'a
dokunulmadı. IR: `__tup_float_float = type { double, double }`, `yon` içinde 0 tahsis çağrısı.
`tests/tuple_return.test.tpr` 11/11, `tests/tuple_hatalari.sh` 9/9 (suites içinde),
`examples/44_coklu_donus.tpr`, fmt idempotent. LSP `__` önekli geçicileri gizler.

Planlanan tasarım (uygulanan hâliyle):

```tpr
func yon(...): (float, float) { ... return hx, hz; }
float dx, dz = yon(...);   //  dx, dz = yon(...);
```

Ayrıştırıcı `(float, float)` için `struct __tup_float_float { float _0; float _1; }` sentezler
(ad yalnız tiplerden; programın başına bir kez). P0.3 sayesinde native `{double,double}`,
res-ptr ABI ile döner → sıfır heap tahsisi. `return a, b;` → `{ __T __r; __r._0 = a; …; return __r; }`.
`float dx, dz = f()` → `__T __t = f(); float dx = __t._0; float dz = __t._1;` — bir `Block`'a
sarılamaz (kapsam), bu yüzden Parser'a `pending_after_` eklenir ve `parse_block`/`parse()`
döngüleri her deyimden sonra boşaltır. `dx, dz = f()` ve `var a, b = f()` callee imzasını ister →
aynı ön tarama (`func AD (…) : ( T, T )`). v1: callee doğrudan adlandırılmış fonksiyon; tuple
bütün olarak bağlanamaz.

Doğrulama: `tests/tuple_return.test.tpr`, `tests/typeinfer/fail/2x_tuple_arity.tpr`,
`examples/44_tuple.tpr`; IR kanıtı: çağrı yerinde `aot_arena_alloc`/`malloc` yok.

## Motor tarafı (tulpar-engine, ayrı PR)

`engine_aksiyon.tpr`: `enum Ekran/Durum/Hal`, `yon_hesapla(): (float, float)`; düşman kaydı
11 paralel dizi olarak kalır (P1 typed dizi bekleniyor — boxed dizide struct her kare 11
string-anahtarlı unpack/box demek). Kapı: otopilot özeti önce/sonra aynı
(`kare=3200 bolum=2 gecis=1 oldurulen=9 kalan_dusman=0 can=26 skor=900 navmesh=true hata=0`).

## P1–P3 (bu planın dışında)

P1 typed struct dizileri `Dusman[]`, iç içe struct kutusuz, `f32`, köprüde `Vec3` register
geçişi + callback. P2 frame arena (`@frame`), `unsafe { ptr<T> }`, atomikler. P3 `@no_alloc`,
`@repr(C)`, `tulpar analyze`, Tracy/LLDB.
