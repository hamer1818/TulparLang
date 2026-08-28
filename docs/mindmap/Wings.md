---
tags: [moc, wings, stdlib]
---

# Wings — HTTP Framework

`lib/wings.tpr` — projenin en aktif geliştirilen parçası. "Give your API wings 🪶".

## Alt konular
- **Cookbook + düzeltmeler (2026-06-22):** docs sitesinde **Wings Cookbook** (12 tarif, EN+TR, hepsi canlı curl ile doğrulandı). İki düzeltme: (1) `static("/", "./public")` artık çalışıyor — `_wings_static_try` dir+rel'i tam bir `/` ile birleştiriyor + dizin-istek `index.html` sunuyor (SPA kökten); (2) özel yanıt deseni netleşti: handler'dan ham `http_create_response` **döndürmek dispatcher'da JSON'a sarılır** — doğrusu `_raw`/`_content_type` envelope'u ya da `wings_current_fd()` + `socket_send` + `{_stream:1}`. → [[Wings Serve Modes]]
- [[Wings Serve Modes]] — `serve`/`listen`/`listen_pool`/`listen_evented`/`listen_async`
- [[Wings Access Log]] — renkli `[saat] METHOD STATUS /path → latency - size`
- [[Wings Validation and Docs]] — `body_schema()` → 422, otomatik `/docs` Swagger UI

## Handler API
- `get/post/put/delete/patch(path, handler)` — handler fonksiyon-referansı; `req` parametresi alır (`req.json`, `req.params.id`, `req["method"]`).
- Yanıt yardımcıları: `ok(data)`, `created(data)` (201), `no_content()` (204), `bad_request(msg)` (400), `not_found(msg)` (404), `server_error(msg)` (500), `text(body)`.
- `serve()` portsuz → varsayılan **8484** (ASCII 'T'=84, "Tulpar portu"); doluysa otomatik +1. → [[Decisions]]

## Auto-persist
Handler global'e veri yazınca (`push(_users, u)`, `_users[i]["name"]=...`) **otomatik kalıcılaşır** — manuel `persist()` gerekmez. Runtime write barrier sayesinde. → [[Memory Model]]

## CORS / keep-alive / HEAD
OPTIONS preflight → otomatik 204 + CORS başlıkları. `http_should_keepalive`. HEAD → GET handler'a fallback + body strip.

## Performans
~40k RPS (pool, in-memory), ~58k (evented), sub-ms latency, 7-8 MB RSS. → [[Performance]]

## İlgili
[[Memory Model]] · [[Memory Leak Fixes]] · [[Router]] · [[Performance]]
