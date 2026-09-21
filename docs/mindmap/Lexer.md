---
tags: [component, frontend]
---

# Lexer

`src/lexer/` — UTF-8 kaynağı `Token*` dizilerine böler.

## Bilinmesi gerekenler
- `read_string` escape switch: standart escape'lere ek **`\e` → ESC (0x1b)** ve **`\0` → NUL** desteklenir (renkli terminal çıktısı için eklendi → [[Wings Access Log]]).
- t-string (template string) escape'leri parser tarafında ayrı işlenir (`build_tstring`, `src/parser/parser.cpp`) — orada da `\e` var.
- `.bak` dosyaları ölü snapshot, yok say.
- **Yeni token HEP `TulparTokenType`'ın SONUNA** (`lexer.hpp` içindeki renumaralama notu: değerler önceden derlenmiş wasm/android arşivlerine sızıyor). `TOKEN_CONST` ve `TOKEN_ENUM` (2026-09-21) böyle eklendi.
- **Yeni anahtar kelime eklemeden önce `grep -rnw --include='*.tpr'`:** `sabit` bir değişken adını çaldığı için reddedildi; `enum`/`sayım`/`sayim` iki depoda da temiz çıktı.

## İlgili
[[Parser]] · [[Architecture]] · [[Wings Access Log]]
