---
tags: [component, frontend]
---

# Parser

`src/parser/` — elle yazılmış **recursive descent**. AST düğümleri `ast_nodes.hpp`, visitor arayüzü `ast_visitor.hpp`.

## Bilinmesi gerekenler
- **Default arguments:** parser/typeinfer eksik trailing arg'lara izin verir; codegen boxed 0 ile padler. typeinfer yalnız **fazla** arg'da hata verir. → `serve()` gibi 0-arg çağrılar bu sayede çalışıyor. ([[Wings]])
- **`m.func(args)` rewrite:** `parse_postfix`, `<id1>.<id2>(args)` → `FunctionCall("<id1>__<id2>", args)`. Modül-niteli çağrı varsayımı; gerçek obje method'u (`obj.method()`) desteklenmiyor. → [[Imports and Modules]]
- `obj.field` okuma/yazma (`p.x`) ArrayAccess desugar'ına düşer. **İstisna:** baş bir `enum` adıysa (`Ekran.MENU`) `parse_postfix` onu **IntLiteral'e katlar** (P0.2, 2026-09-21); bilinmeyen üye ayrıştırma hatası.
- **Çoklu dönüş / tuple tamamen ayrıştırıcı şekeri (P0.1, 2026-09-21):** `func f(): (float, float)` → dönüş tipi sentezlenmiş `struct __tup_float_float { float _0; float _1; }` (ad yalnız tiplerden; `parse()` sonunda TypeDecl'ler programın başına). `return a, b;` → `{ __T __rN; __rN._0 = a; …; return __rN; }` (Block). `float a, b = f();` / `a, b = f();` → `__T __tN = f();` + **`pending_after_`** deyimleri (`float a = __tN._0; …`) — bir Block'a sarılamaz (kapsam), `parse_block`/`parse()` her deyimden sonra boşaltır, süslü parantezsiz gövdeler `reject_pending` ile reddeder. Callee imzası `prescan_tuple_sigs` ile (aynı dosya, adlandırılmış fonksiyon — v1). Lambda gövdesi tuple bağlamını görmez. → `plans/08_oyun_dili_p0.md`
- **`enum` tamamen ayrıştırıcı şekeri:** `parse()` başında `prescan_enums` token dizisini tarayıp tabloyu doldurur (bildirim sırası önemsiz), `parse_type` enum adını `TYPE_INT`'e çözer, `parse_enum_decl` sert denetimi yapar (yalnız üst düzey, yinelenen üye/ad, tamsayı olmayan değer). Düğüm `EnumDecl`/`AST_ENUM_DECL` olarak kalır: codegen no-op, LSP üyeleri buradan indeksler. → `plans/08_oyun_dili_p0.md`
- t-string escape switch'i burada (`build_tstring`), `\e` dahil. → [[Lexer]]

## İlgili
[[Lexer]] · [[Type Inference]] · [[AOT Backend]] · [[Imports and Modules]]
