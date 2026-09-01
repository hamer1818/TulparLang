---
tags: [moc, architecture]
---

# Architecture — Derleme Pipeline'ı

Yukarıdan aşağıya tek yön (AOT-only; VM/interpreter kaldırıldı → [[Decisions]]).

```
.tpr kaynak
  → [[Lexer]]            src/lexer/      UTF-8 → Token*
  → [[Parser]]           src/parser/     recursive descent → AST (ast_nodes.hpp)
  → [[Type Inference]]   src/typeinfer/  AST üzerinde, codegen öncesi
  → [[AOT Backend]]      src/aot/        LLVM IR üretimi → native binary
  → [[Runtime]]          src/vm/ + runtime/  AOT binary'nin link'lediği destek
```

## Önemli notlar
- **Tek backend:** AOT/LLVM. Yeni dil özelliği yalnız `src/aot/` + lexer/parser/typeinfer'e eklenir; paralel bytecode yolu YOK. → [[Decisions]]
- **`src/vm/` adı yanıltıcı:** artık VM değil, **paylaşılan runtime** (`runtime_bindings.cpp` = tüm `aot_*` builtin'leri, `vm.cpp` = arena/allocator'lar, `vm.hpp` = `VMValue`/`Obj` tipleri). → [[Runtime]]
- **İki CMake hedefi:** `tulpar` (derleyici) ve `tulpar_runtime` (AOT binary'lerin link'lediği static lib, `-DTULPAR_RUNTIME_ONLY`). İkisi de derlenebilir kalmalı. → [[Build System]]
- Type inference her `tulpar` çağrısında `[typecheck]` uyarısı basar; `--no-typecheck` ile kapatılır.

## İlgili
[[Async Runtime]] · [[Memory Model]] · [[Standard Library]] · [[Cross-platform]]
