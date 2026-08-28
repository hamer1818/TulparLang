---
tags: [subsystem, tooling]
---

# LSP

`src/lsp/` — `tulpar --lsp` (stdio JSON-RPC). Banner'dan önce dispatch edilmeli (stdin/stdout'a sahip).

## Parçalar
- `document_index.cpp` — her değişiklikte reparse.
- `builtins.cpp` — native builtin sembol tablosu (completion/hover). Yeni builtin eklerken imza buraya da yazılır (örn. `clock_ms`, `db_open`, `mod`, `toInt`, `substring`). Not: `arena_*` burada YOK, codegen'de isimle tanınır.

## İlgili
[[AOT Backend]] · [[Tooling]] · [[Architecture]]
