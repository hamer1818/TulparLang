---
tags: [component, frontend]
---

# Parser

`src/parser/` — elle yazılmış **recursive descent**. AST düğümleri `ast_nodes.hpp`, visitor arayüzü `ast_visitor.hpp`.

## Bilinmesi gerekenler
- **Default arguments:** parser/typeinfer eksik trailing arg'lara izin verir; codegen boxed 0 ile padler. typeinfer yalnız **fazla** arg'da hata verir. → `serve()` gibi 0-arg çağrılar bu sayede çalışıyor. ([[Wings]])
- **`m.func(args)` rewrite:** `parse_postfix`, `<id1>.<id2>(args)` → `FunctionCall("<id1>__<id2>", args)`. Modül-niteli çağrı varsayımı; gerçek obje method'u (`obj.method()`) desteklenmiyor. → [[Imports and Modules]]
- `obj.field` okuma/yazma (`p.x`) ArrayAccess desugar'ına düşer.
- t-string escape switch'i burada (`build_tstring`), `\e` dahil. → [[Lexer]]

## İlgili
[[Lexer]] · [[Type Inference]] · [[AOT Backend]] · [[Imports and Modules]]
