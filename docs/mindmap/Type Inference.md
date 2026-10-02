---
tags: [component, frontend]
---

# Type Inference

`src/typeinfer/` — AST üzerinde, codegen öncesi çalışır.

## Bilinmesi gerekenler
- Her `tulpar` / `tulpar build` çağrısında `typeinfer_emit_warnings` ön-geçişi `[typecheck]` uyarıları basar. `tulpar typecheck` aynı denetleyicinin hata modu.
- Kapatma: `--no-typecheck` veya `TULPAR_NO_TYPECHECK=1` (yeni kural şekillendirirken).
- **Struct aritmetiği tanılanır (2026-10-02):** dilde struct için işleç yok; bilinen bir struct `+`'nın öbür tarafı dizgi değilse ya da `- * / %` operandıysa `[typecheck]`. Ölçülen sessiz sonuçlar: `p + 5` / `p * 2` → 0, `p + v` (json/var) ve `p + q` → `"P {...}5"` dizgisi (#451'den beri). Dizgi birleştirmesi (`"p=" + p`) serbest. Tipi dinamik öbür taraf ayrı cümleyle (`toString(p) + v` önerir). Fikstürler: `fail/32_struct_arithmetic.tpr`, `pass/25_struct_concat_ok.tpr`.
- **Default-arg gevşetmesi:** arg-sayısı denetimi `if (got > expected)` (eskiden `!=`); tip-denetim döngüsü `i < got && i < expected` ile sınırlı. → [[Parser]]

## İlgili
[[Parser]] · [[AOT Backend]] · [[Architecture]]
