---
tags: [wings, stdlib]
---

# Wings — Validation & Auto-Docs

## Request validation → 422
- `body_schema(schema)` ile route'a şema bağlanır.
- `_wings_validate` → geçerliyse `{}`, değilse `{"_status":422, "error":"validation failed", "fields":{...}}`.
- `_wings_has_key`, `_wings_type_ok` — tip + required kontrolü.
- Açık: nested object/array, min/max/regex henüz yok. → [[Roadmap]]

## Otomatik /docs (Swagger UI)
- `listen()`/`listen_pool()` route kaydından **OpenAPI** üretir: `_wings_oapi_type/body/path/params`, `wings_openapi` (requestBody + params + 422 dahil).
- `docs_info(title, version)`, `_wings_openapi_json`, `_wings_docs_html` (Swagger UI HTML).
- `/docs` + `/openapi.json` otomatik kaydedilir; opt-out **`TULPAR_WINGS_NODOCS=1`** (veya path'i kendin tutarsan).
- `/healthz`, `/metrics` de otomatik eklenir.

## İlgili
[[Wings]] · [[Router]] · [[Roadmap]]
