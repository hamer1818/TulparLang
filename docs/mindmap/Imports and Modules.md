---
tags: [component, frontend]
---

# Imports & Modules

`import "name"` çözümleme sırası (`src/aot/llvm_backend.cpp` import handler): gömülü stdlib adı → `name` → `name.tpr` → `tulpar_modules/<name>/<name>.tpr` → `tulpar_modules/<name>.tpr`. Son ikisi `tulpar pkg install`'un kullandığı yer.

## Alias'lı import
`import "name" as alias;` → modüldeki her top-level `func` `<alias>__<name>`'e yeniden adlandırılır, intra-module çağrılar da. İki kütüphane aynı `route`/`helper`'ı export etse çakışmaz. Builtin'ler ve importer'ın kendi fonksiyonları dokunulmaz. Rewrite: `src/parser/import_alias.cpp` (AOT `AST_IMPORT` codegen'den çağrılır).

## Çağrı biçimleri
`m.func(args)` (Python tarzı) ≡ `m__func(args)` (mangled). `parse_postfix` `<id>.<id>(args)` → tek `FunctionCall`. `obj.field` okuma/yazma ayrı (ArrayAccess desugar). **Gerçek obje method'u (`obj.method(x)`) desteklenmiyor.** → [[Parser]]

## İlgili
[[Parser]] · [[Standard Library]] · [[Tooling]]
