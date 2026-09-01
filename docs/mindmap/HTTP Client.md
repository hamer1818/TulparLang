---
tags: [stdlib, async]
---

# HTTP Client

`lib/http_client.tpr` — istemci tarafı HTTP. Non-blocking varyant [[Async Runtime]] event loop'u üzerinde.

## Bilinmesi gerekenler
- `aot_http_request_async` (runtime) — blocking isteği küçük bir worker pool'a offload eder; network bacağı worker'da, parse (`http_request_url` → VM obje) **main thread'de** (worker'dan VM allocate güvenli değil).
- `examples/37_async_http.tpr` — async HTTP örneği.

## İlgili
[[Async Runtime]] · [[Standard Library]] · [[Wings]]
