---
tags: [stdlib]
---

# Router

`lib/router.tpr` — Wings'in altında / ondan bağımsız kullanılabilen route eşleme.

## Bilinmesi gerekenler
- İki-geçiş dispatch: exact match + pattern (`:id` segmentleri) → `{"index":..., "params":...}`.
- `_find_route` / `_find_route_with_params`.
- Hash tablo yerine lineer tarama (bağımlılıktan kaçınmak için); küçük route setlerinde yeterli.
- Testler: `tests/router.test.tpr` (18/18).

## İlgili
[[Wings]] · [[Wings Validation and Docs]] · [[Standard Library]]
