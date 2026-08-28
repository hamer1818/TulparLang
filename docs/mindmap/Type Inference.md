---
tags: [component, frontend]
---

# Type Inference

`src/typeinfer/` — AST üzerinde, codegen öncesi çalışır.

## Bilinmesi gerekenler
- Her `tulpar` / `tulpar build` çağrısında `typeinfer_emit_warnings` ön-geçişi `[typecheck]` uyarıları basar. `tulpar typecheck` aynı denetleyicinin hata modu.
- Kapatma: `--no-typecheck` veya `TULPAR_NO_TYPECHECK=1` (yeni kural şekillendirirken).
- **Default-arg gevşetmesi:** arg-sayısı denetimi `if (got > expected)` (eskiden `!=`); tip-denetim döngüsü `i < got && i < expected` ile sınırlı. → [[Parser]]

## İlgili
[[Parser]] · [[AOT Backend]] · [[Architecture]]
