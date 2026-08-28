---
tags: [subsystem, tooling]
---

# Tooling — fmt / pkg / update

CLI dispatch `src/main.cpp`'de; `--lsp`, `fmt`, `pkg`, `version`, `--help`, `update` run/build yolundan önce short-circuit eder.

## Formatter
`src/fmt/` — `tulpar fmt script.tpr`.

## Package Manager
`src/pkg/` — `tulpar pkg <init|add|install>`. `manifest.cpp` `tulpar.toml` okur; `pkg_cli.cpp` `path:` bağımlılıkları `tulpar_modules/<name>/`'e vendor'lar (registry bağımlılıkları TODO). → [[Imports and Modules]]

## Self-update
`src/cli/update_cmd.cpp` — `tulpar update [--check]`, tulparlang.dev'den.

## İlgili
[[LSP]] · [[Imports and Modules]] · [[Build System]]
