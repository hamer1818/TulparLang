# Wings + ORM Cheatsheet

> Tek sayfada tüm public API — tercih edilen (kısa/UFCS) isimlerle.
> Eski isimler çalışmaya devam eder (parantez içinde); yeni kod kısa olanı kullanır.
> Ayrıntılı tasarım gerekçesi: [WINGS_DX.md](WINGS_DX.md).

## 7 satırda kalıcı CRUD API

```tpr
import "wings";
import "orm";

database("notes.db");
json Note = model("notes", {"id": "pk", "title": "str!", "done": "bool"});

resource("/notes", Note);   // GET/POST /notes, GET/PUT/DELETE /notes/:id + 422 + /docs
serve();                    // :8484
```

## Route kaydı

| Çağrı | İş |
|---|---|
| `get(path, handler)` | GET route — handler fonksiyon referansı: `get("/users", list_users)` |
| `post / put / patch / head / options` | Diğer HTTP fiilleri, aynı imza |
| `delete(path, handler)` (`del`) | DELETE route |
| `resource(path, Model[, opts])` | Modelden 5 CRUD route + şema + docs. `opts: {"only": [...]}` / `{"except": [...]}` (aksiyonlar: index/show/create/update/destroy) |
| `group(prefix, register_fn)` | Route grubu: prefix altında toplu kayıt |
| `cached_get(path, handler)` | Cache'li GET + otomatik ETag→304 |
| `accepts(schema)` (`body_schema`) | Son route'a istek şeması: `accepts({"name": "str", "age?": "int"})` → uymayan gövde 422 |
| `returns(schema)` (`response_model`) | Son route'a cevap şeması — listelenmeyen alan cevaptan düşer (sır sızdırmaz) |
| `static(url_prefix, dir)` | Statik dosya servis |

## Sunucu

| Çağrı | İş |
|---|---|
| `serve()` | 8484 ("Tulpar portu"), doluysa +1 |
| `serve(8080)` | Açık port |
| `serve(8080, 4)` | 4 worker'lı thread pool |
| Gelişmiş: `listen / listen_pool / listen_async / listen_evented / wings_tls(port, crt, key)` | Zamanlama modelini elle seç |
| `docs_info(title, version)` | /docs başlık + sürüm |

## İstek okuma — UFCS stili (`req.x(...)`)

| Çağrı | İş |
|---|---|
| `req.json` | Parse edilmiş gövde (otomatik) |
| `req.params.id` | Path parametresi (ham string) |
| `req.param("id", "")` | Path parametresi, fallback'li — tipli: `req.param_int("id", 0)`, `req.param_bool(...)` |
| `req.query("q", "")` | Query-string — tipli: `req.query_int("page", 1)`, `req.query_bool(...)` |
| `req.form("name", "")` | Form alanı (urlencoded/multipart); tümü: `form_data(req)`, dosyalar: `uploaded_files(req)` |
| `req.cookies()` (`wings_cookies`) | Çerez dict'i |
| `get_signed_cookie(req, name, secret)` | İmzalı çerez oku ("" = yok/oynanmış) |
| `req["jwt"]` | `jwt_guard` sonrası claims |

## Cevap üretme

| Çağrı | Durum |
|---|---|
| `ok(data)` | 200 |
| `created(data)` | 201 |
| `no_content()` | 204 |
| `bad_request / unauthorized / forbidden / not_found / conflict / server_error (msg)` | 400/401/403/404/409/500 |
| `text(body)` · `html(body)` · `render(tpl, vars)` | text/plain · text/html · {{var}} şablonu |
| `redirect(url)` · `redirect_status(url, code)` | 302 · özel kod |
| `with_status(data, code)` · `with_headers(data, hdrs)` | Zarf ayarları |
| `set_cookie(res, name, val, opts)` · `delete_cookie(res, name)` | Çerez yaz/sil |
| `set_signed_cookie(res, name, val, secret, opts)` | HMAC imzalı çerez |

## Middleware / özellikler (startup'ta bir kez)

| Çağrı | İş |
|---|---|
| `use(mw)` | Global middleware (`func mw(req)` — dict dönerse kısa devre) |
| `cors(origin, opts)` | CORS (credentials/methods/headers/expose/max_age) |
| `rate_limit(max, window_s)` | Sabit pencere, IP bazlı 429 |
| `gzip([min_bytes])` (`enable_gzip`) | Yanıt sıkıştırma |
| `jwt_guard(secret)` + `jwt_public(path)` | Bearer-token koruması + istisna path'ler |
| `session_start(req, secret)` / `session_attach(res, sid, secret)` / `session_set/get(sid, key[, val])` / `session_destroy(sid)` | Server-side session |
| `depends(fn)` + `dep(name)` | Route bağımlılık enjeksiyonu |

## Streaming / ops

| Çağrı | İş |
|---|---|
| `sse_headers()` · `sse_event(name, data)` (`wings_sse_*`) | SSE başlıkları · event frame'i |
| `ws_upgrade(req)` (`wings_ws_upgrade`) | WebSocket el sıkışması |
| `ws_send(fd, s)` · `ws_close(fd)` · `ws_pong(fd, s)` (`wings_ws_send_*`) | WS frame gönderimi |
| `metrics_prom()` (`wings_metrics_prom`) | Prometheus metin formatı |
| `log_info(msg)` · `log_error(msg)` | Yapılandırılmış log |
| Otomatik: `/healthz`, `/metrics`, `/docs`, `/openapi.json` | Kapatma: kendi route'unu kaydet / `TULPAR_WINGS_NODOCS=1` |

## ORM (`import "orm"`) — model handle + UFCS

```tpr
database("app.db");                 // sonraki model()'lerin bağlantısı
json User = model("users", {
    "id":    "pk",                  // INTEGER PRIMARY KEY AUTOINCREMENT
    "name":  "str!",                // TEXT NOT NULL   (str/int/float/bool + `!`)
    "age":   "int",
    "vip":   "bool",                // okurken true/false döner
    "email": "TEXT UNIQUE"          // tanınmayan tanım ham SQL olarak geçer
});
```

| Çağrı | İş |
|---|---|
| `User.create({...})` | Parametreli INSERT → yeni id. Şema dışı anahtar düşer; `0`/`""`/`false` da yazılır |
| `User.find(id)` | Tek satır (tipler cast'li) ya da `{}` |
| `User.all()` | Tüm satırlar |
| `User.where("age > ? AND vip = ?", [18, 1])` | Bağlı parametreli filtre — değer SQL'e asla gömülmez |
| `User.first(cond, params)` | İlk eşleşen ya da `{}` |
| `User.count()` | Satır sayısı |
| `User.update(id, {...})` | Parametreli UPDATE |
| `User.save(obj)` | Upsert: `id` var + kayıt var → update, yoksa create; taze satır döner |
| `User.remove(id)` | DELETE |
| `User.raw("age > 18")` | HAM where — kaçış kapısı, kullanıcı girdisi GÖMME |
| v1 uyumluluk | `orm_open / define_model / orm_create / orm_find / orm_all / orm_where / orm_update / orm_delete` çalışmaya devam eder |

## Adlandırma kuralları (yeni API eklerken)

1. `_` önekli her şey iç API; public'te `wings_` öneki yok.
2. İstek okuyanların ilk parametresi `req` → `req.x(...)` diye belgelenir.
3. Özellik açan çağrı özelliğin adıdır: `cors()`, `gzip()`, `rate_limit()`.
4. Cevap helper'ları HTTP sözcükleri: `ok/created/not_found/...`
5. Tipli varyant sonek alır: `param_int`, `query_bool`.
6. Kavram başına tek sözcük: `accepts`/`returns` çifti gibi.
7. Rename kırmaz: yeni ad esas, eski ad sarmalayıcı olarak kalır.
