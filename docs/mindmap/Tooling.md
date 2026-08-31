---
tags: [subsystem, tooling]
---

# Tooling — fmt / pkg / update

CLI dispatch `src/main.cpp`'de; `--lsp`, `fmt`, `pkg`, `version`, `--help`, `update` run/build yolundan önce short-circuit eder.

## Formatter
`src/fmt/` — `tulpar fmt script.tpr`.

## Package Manager
`src/pkg/` — `tulpar pkg <init|add|install|list|remove|search>`. `manifest.cpp` `tulpar.toml` okur; `pkg_cli.cpp` `path:` bağımlılıkları `tulpar_modules/<name>/`'e vendor'lar, `url:` tek dosya çeker, gerisi registry'den (`fetch_versions` + indirme — "registry TODO" notu BAYATTI). Bağımlılık sözdizimi bir DİZGİ: `mathx = "path:../dir"`; inline tablo (`{ path = ... }`) desteklenmiyor ve net hata veriyor. Denetim: `tests/pkg_audit.sh` (`build.sh suites`) — init/add/install zinciri, vendor edilenin GERÇEKTEN import edilebilmesi, ve hata yollarının sıfırdan farklı dönmesi. → [[Imports and Modules]]

## Self-update
`src/cli/update_cmd.cpp` — `tulpar update [--check]`, tulparlang.dev'den.

## İlgili
[[LSP]] · [[Imports and Modules]] · [[Build System]]
