---
tags: [moc, stdlib]
---

# Standard Library (lib/)

`lib/*.tpr` derleme zamanında `src/embedded_libs.h`'a gömülür (`cmake/EmbedLibraries.cmake` → `configure_file` `embedded_libs.h.in`). `src/embedded_libs.h` generated (gitignored) — elle düzenleme; `.in` + `lib/*.tpr` düzenle.

## Gömülü modüller
[[Wings]] · [[Router]] · [[ORM]] · [[HTTP Client]] · `http_utils` · `async` · `middleware` · `socket` · `tulpar_api` · `test`

## Yeni modül eklemek
1. `lib/<ad>.tpr` oluştur
2. `cmake/EmbedLibraries.cmake`'e `embed_library(...)`
3. `embedded_libs.h.in`'e slot

## ⚠️ WSL build uyarısı
`lib/*.tpr` değişince incremental build gömülüyü stale bırakabiliyor (saat kayması) → **`./build.sh clean`** gerekir. → [[Build System]]

## İlgili
[[Build System]] · [[Imports and Modules]] · [[Wings]]
