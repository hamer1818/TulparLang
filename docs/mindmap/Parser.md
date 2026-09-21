---
tags: [component, frontend]
---

# Parser

`src/parser/` — elle yazılmış **recursive descent**. AST düğümleri `ast_nodes.hpp`, visitor arayüzü `ast_visitor.hpp`.

## Bilinmesi gerekenler
- **Default arguments:** parser/typeinfer eksik trailing arg'lara izin verir; codegen boxed 0 ile padler. typeinfer yalnız **fazla** arg'da hata verir. → `serve()` gibi 0-arg çağrılar bu sayede çalışıyor. ([[Wings]])
- **`m.func(args)` rewrite:** `parse_postfix`, `<id1>.<id2>(args)` → `FunctionCall("<id1>__<id2>", args)`. Modül-niteli çağrı varsayımı; gerçek obje method'u (`obj.method()`) desteklenmiyor. → [[Imports and Modules]]
- `obj.field` okuma/yazma (`p.x`) ArrayAccess desugar'ına düşer. **İstisna:** baş bir `enum` adıysa (`Ekran.MENU`) `parse_postfix` onu **IntLiteral'e katlar** (P0.2, 2026-09-21); bilinmeyen üye ayrıştırma hatası.
- **`enum` tamamen ayrıştırıcı şekeri:** `parse()` başında `prescan_enums` token dizisini tarayıp tabloyu doldurur (bildirim sırası önemsiz), `parse_type` enum adını `TYPE_INT`'e çözer, `parse_enum_decl` sert denetimi yapar (yalnız üst düzey, yinelenen üye/ad, tamsayı olmayan değer). Düğüm `EnumDecl`/`AST_ENUM_DECL` olarak kalır: codegen no-op, LSP üyeleri buradan indeksler. → `plans/08_oyun_dili_p0.md`
- t-string escape switch'i burada (`build_tstring`), `\e` dahil. → [[Lexer]]

## İlgili
[[Lexer]] · [[Type Inference]] · [[AOT Backend]] · [[Imports and Modules]]
