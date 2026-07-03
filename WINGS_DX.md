# Wings & ORM Yazım Kolaylığı Çalışması (DX)

> **Tarih:** 2026-07-03 · **Durum:** Tasarım çalışması — uygulama bekliyor
> **Motto testi:** "Python kadar kolay, C kadar hızlı." Bu doküman, Wings'te
> "kolay" tarafının neresinin aksadığını ölçer ve kapatma planını verir.

Ölçüt ikili: **(1) satır sayısı** — aynı işi FastAPI/Express'ten daha az veya
eşit satırda yazabiliyor muyuz; **(2) tahmin edilebilirlik** — kullanıcı bir
fonksiyonun adını *dokümana bakmadan* içgüdüyle bulabiliyor mu.

---

## 1. Bulgular — mevcut durum

### 1.1 Dil bugün zaten ne veriyor (önemli: çoğu taş yerinde)

Bu çalışmanın en kritik bulgusu şu: önerilen iyileştirmelerin **hiçbiri
derleyici değişikliği gerektirmiyor**. Gereken dil özellikleri son turlarda
zaten eklendi ve testli:

| Özellik | Kanıt | DX için anlamı |
|---|---|---|
| UFCS: `obj.method(a)` → `method(obj, a)` | `tests/methods.test.tpr` | `Note.find(1)`, `req.query("q")` yazımı **bugün** mümkün |
| Fonksiyon-ref handler (`get("/u", list_users)`) | `tests/funcref.test.tpr`, `compare_wings_users_api.tpr` | String handler devri kapandı; typo = derleme hatası |
| Parametreli SQL (`db_query(db, sql, [params])`) | `tests/db_params.test.tpr` (v3.3.0) | ORM'in elle quote-escape'ine gerek yok |
| Default arg (eksik arg → 0 pad) | `tests/defaultargs.test.tpr` | `serve()` / `serve(port)` / `serve(port, workers)` tek fonksiyon olabilir |
| Lambda / closure | `tests/closure_basic.test.tpr` | (İleride) inline handler potansiyeli |
| t-string, auto-persist, `req.params.id` | STATUS "Wings ergonomisi" | Handler gövdeleri zaten kısa |

Yani sorun dilde değil, **kütüphane API'sinin bu özelliklerin gerisinde
kalmış olmasında**. İş saf Tulpar (`lib/*.tpr`) + LSP tablosu güncellemesi.

### 1.2 ORM'in bugünkü hali (`lib/orm.tpr`, 188 satır)

`orm_open / define_model / orm_create / orm_find / orm_all / orm_where /
orm_update / orm_delete`. Tespitler:

1. **Parametreli SQL kullanmıyor.** ORM, v3.3.0'daki `db_query(db, sql,
   [params])` eklenmeden yazılmış; hâlâ `'` ikileme + string birleştirme ile
   SQL kuruyor. `orm_where(table, where_sql)` ham fragment alıyor ve
   sanitizasyonu çağırana bırakıyor — güvenlik primitifleri turu (v3.3.0+)
   ile çelişen tek stdlib parçası bu.
2. **Sıfır/boş değer yazılamıyor (gerçek bug).** `orm_create`/`orm_update`
   kolonları `if (v)` truthiness ile eliyor; `{"done": 0}` veya `{"name": ""}`
   **sessizce düşer**. Somut sonuç: bir todo'yu `orm_update` ile "geri
   açamazsın" (`done: 0` yazılmaz). Kaynakta "MVP limitation" diye not
   düşülmüş; artık kapatılmalı.
3. **İsim tutarsız:** `define_model` öneksiz, kalanı `orm_*`. Ne tam
   namespace, ne tam kısa.
4. **Tek global DB** (`_orm_db`), ikinci veritabanı açılamıyor.
5. **Satır normalizasyonu yok:** SQLite'tan dönen `done` 0/1 int; her örnek
   kendi `row_to_note()` çeviricisini elle yazıyor.
6. **LSP'de görünmüyor:** `src/lsp/builtins.cpp`'de wings sembolleri kayıtlı
   (serve, ok, not_found…) ama **hiçbir `orm_*` girdisi yok** → completion ve
   hover'da ORM yok; kullanıcı var olduğunu keşfedemiyor.
7. **Amiral gemisi örnek ORM'i kullanmıyor:** `examples/wings_notes_db.tpr`
   (175 satır) elle `sql_str()` yazıyor, string-handler stiliyle kayıt
   yapıyor ve yorumları "parametreli sorgu YOK" diyor — v3.3.0'dan beri
   yanlış. Vitrindeki örnek, en zor yolu öğretiyor.

### 1.3 Wings adlandırma denetimi (`lib/wings.tpr`, 2836 satır, ~70 public fn)

İyi olan çok: `get/post/put/patch`, `ok/created/not_found/...`,
`cors()/rate_limit()`, `param/query/form` + `_int/_bool` son ekleri,
`serve()`. Tutarsızlıklar şunlar:

| # | Tutarsızlık | Örnekler |
|---|---|---|
| A | **Karışık önek:** public API'nin çoğu öneksizken 9 fonksiyon `wings_` taşıyor | `wings_cookies`, `wings_sse_headers/event`, `wings_ws_upgrade/send_text/send_close/send_pong`, `wings_metrics_prom`, `wings_openapi` |
| B | **Fiil stili karışık:** özellik açan çağrılar isim-stili (`cors()`, `rate_limit()`) ama gzip fiilli | `enable_gzip` |
| C | **Kavram başına iki sözcük:** istek şeması "schema", cevap şeması "model" | `body_schema` vs `response_model` |
| D | **HTTP fiiliyle eşleşmeyen tek route fonksiyonu** | `del` (oysa `delete` keyword değil — lexer'da yok, kullanılabilir) |
| E | **Beş sunucu girişi:** hangisinin "doğru" olduğu belirsiz | `serve`, `listen`, `listen_pool`, `listen_evented`, `listen_async` |
| F | **Okuma/yazma simetrisi bozuk:** çerez yaz `set_cookie(res,…)`, imzalı oku `get_signed_cookie(req,…)`, düz oku `wings_cookies(req)` | — |
| G | **UFCS hiç belgelenmemiş:** `param(req, "id")` zaten `req.param("id")` diye çağrılabiliyor (ilk parametre `req`) ama hiçbir örnek/doc bu stili göstermiyor | tüm `req` okuyucuları |

---

## 2. Öneri 1 — ORM v2: model handle + UFCS

### Hedef API

```tpr
import "orm";

database("app.db");                       // db_open sarmalayıcı; birden çok kez çağrılabilir

json Note = model("notes", {
    "id":    "pk",                        // INTEGER PRIMARY KEY AUTOINCREMENT
    "title": "str!",                      // TEXT NOT NULL
    "done":  "bool",                      // INTEGER 0/1 — okurken true/false döner
    "score": "float"                      // REAL
});

int id     = Note.create({"title": "süt al"});     // → param SQL, last_insert_id
json n     = Note.find(id);                        // {} = yok; tipler cast'li (done: bool)
array all  = Note.all();
array open = Note.where("done = ?", [0]);          // HER ZAMAN bağlı parametre
json f     = Note.first("done = ?", [0]);          // ilk satır ya da {}
int c      = Note.count();
Note.update(id, {"done": 0});                      // 0/"" artık YAZILIR (keys() iterasyonu)
Note.remove(id);
json row   = Note.save({"id": 3, "title": "x"});   // id var+kayıt var → update, yoksa create
```

`Note.find(1)` UFCS ile `find(Note, 1)`'e çözünür — **model handle sıradan bir
json**: `{"table": "notes", "db": <handle>, "schema": {...}}`. Derleyici
değişikliği yok; `lib/orm.tpr` yeniden yazımı yeterli.

### Tip kısayolları — `body_schema` ile ORTAK kelime hazinesi

Kullanıcı zaten `body_schema({"title": "str", "done?": "bool"})` yazıyor;
model şeması aynı sözcükleri kullanmalı ki tek zihinsel model olsun:

| Kısayol | SQL karşılığı | Okumada cast |
|---|---|---|
| `"pk"` | `INTEGER PRIMARY KEY AUTOINCREMENT` | int |
| `"int"` / `"int!"` | `INTEGER` (+ `NOT NULL`) | int |
| `"float"` | `REAL` | float |
| `"bool"` | `INTEGER NOT NULL DEFAULT 0` | **true/false** |
| `"str"` / `"str!"` | `TEXT` (+ `NOT NULL`) | str |
| boşluk/büyük harf içeren her şey | ham SQL geçişi (`"TEXT UNIQUE"` gibi) | dokunulmaz |

Satır cast'i (`bool` → true/false, sayılar → int/float) `find/all/where/first`
dönüşünde otomatik → örneklerdeki el yapımı `row_to_note()` ölür.

### Düzeltilen kusurlar

- **0/boş değer bug'ı:** kolon seçimi `schema` üzerinden truthiness ile değil,
  `keys(attrs)` üzerinden üyelikle yapılır (şemada olmayan anahtar yine
  düşürülür — request body passthrough korunur).
- **Injection yüzeyi:** üretilen her SQL `?` + params ile `db_query/db_execute`
  3-arg formuna gider. `where/first` koşulu yapısal olarak parametre listesi
  ister; ham-fragment kaçış kapısı isteyen `Note.raw("...")` diye ayrı ve adı
  üstünde tehlikeli tek fonksiyona sıkıştırılır.
- **Çoklu DB:** handle model içinde taşındığı için `database()` ikinci kez
  çağrılıp sonraki `model()`'ler ona bağlanabilir.

### Geriye uyumluluk

`orm_open/define_model/orm_create/...` **silinmez** — her biri yeni çekirdeğe
delege eden 1-3 satırlık sarmalayıcı olur. Eski kod kırılmaz; docs yalnızca
yeni stili anlatır.

### İsim çakışma denetimi (yapıldı)

`find, all, create, update, remove, count, first, save, where, model,
database, resource, accepts, returns, cookies, gzip, delete` — builtin
tablosunda (`src/lsp/builtins.cpp`) ve wings public yüzeyinde **hiçbiri
dolu değil** (yalnızca `created` ve `gzip_compress` var, ikisi de farklı).
`delete` lexer'da keyword değil (grep doğrulandı). Plain `import` global
scope'a indiği için kullanıcının kendi `find`'ı ile çakışma *teorik olarak*
mümkün — bugün `orm_find` için de geçerli olan bilinen davranış; docs'ta
"modül fonksiyonları globaldir" notu yeterli.

---

## 3. Öneri 2 — `resource()`: 5 satırda kalıcı CRUD API (amiral gemisi)

wings + orm köprüsü. Motto'nun vitrin cümlesi:

```tpr
import "wings";
import "orm";

database("notes.db");
json Note = model("notes", {"id": "pk", "title": "str!", "done": "bool"});

resource("/notes", Note);    // beş route + 422 şema doğrulama + /docs, otomatik
serve();
```

`resource(path, Model)` şunları kaydeder:

| Route | Davranış |
|---|---|
| `GET /notes` | `Model.all()` → `ok({data, count})` |
| `GET /notes/:id` | `find` → `ok(row)` / `not_found` |
| `POST /notes` | türetilmiş `body_schema` (422 otomatik) → `create` → `created(row)` |
| `PUT /notes/:id` | `update` → `ok(row)` / `not_found` |
| `DELETE /notes/:id` | `remove` → `no_content()` / `not_found` |

- **Şema türetme:** model şemasından — `"str!"` → zorunlu `"title": "str"`,
  `"bool"` → `"done?": "bool"`, `pk` dışarıda. Aynı şema OpenAPI/docs'u da
  besler → Swagger UI bedava.
- **Özelleştirme:** `resource("/notes", Note, {"only": ["index", "show"]})`
  (veya `"except"`). Daha fazlası gerekiyorsa kullanıcı zaten `get("/notes/stats",
  my_handler)` ile yanına route ekler — `resource` tekel değil.
- **Nerede yaşar:** `lib/wings.tpr` içinde. wings, orm'u **import etmez**;
  model handle sözleşmesi (`table/db/schema`) üzerinden `db_*` builtin'leriyle
  konuşan içsel `_wings_res_*` handler'ları kullanır. Gerekçe: `orm` tek başına
  (CLI script'te) sunucu yükü olmadan import edilebilir kalmalı; wings de orm'suz
  çalışmaya devam etmeli. (Alternatif — wings'in orm'u import etmesi
  (`wings_tls` → `wings` emsali var) — CRUD mantığını teke indirir ama çift
  import dedup davranışı doğrulanana kadar riskli; bkz. §6 doğrulama listesi.)

Karşılaştırma: bugünkü eşdeğeri `examples/wings_notes_db.tpr` = **175 satır**
(~90 satır kod). Yeni form: **7 satır**. FastAPI'de aynı iş (model + 5
endpoint + validasyon): ~40 satır.

---

## 4. Öneri 3 — Adlandırma standardı

### Kurallar (bundan sonra her wings/stdlib fonksiyonu için)

1. **`_` ile başlayan = iç API.** Public API'de `wings_` öneki kullanılmaz;
   önek yalnızca `_wings_*` iç fonksiyonlarında yaşar.
2. **İstek okuyanların ilk parametresi `req`** → UFCS ile `req.param("id")`
   diye okunur ve dokümanda **bu stille** gösterilir.
3. **Özellik açan çağrı, özelliğin adıdır:** `cors()`, `rate_limit()`,
   `gzip()`, `jwt_guard()`. `enable_*` / `setup_*` fiil önekleri yasak.
4. **Cevap helper'ları HTTP'nin kendi sözcükleridir:** `ok/created/no_content/
   bad_request/...` (mevcut hali doğru; koru).
5. **Tipli varyant = sonek:** `param_int`, `query_bool` (mevcut hali doğru).
6. **Kavram başına tek sözcük.** İstek şeması + cevap şeması aynı aileden
   adlandırılır: `accepts()` / `returns()`.
7. **Rename asla kırmaz:** yeni ad esas olur, eski ad tek satırlık sarmalayıcı
   olarak kalır (gömülü lib'te deprecation uyarısı gürültü olur; sadece docs
   yeniyi gösterir).

### Rename/alias tablosu

| Bugün | Önerilen | Not |
|---|---|---|
| `wings_cookies(req)` | `cookies(req)` → `req.cookies()` | F simetrisi düzelir |
| `wings_ws_upgrade(req)` | `ws_upgrade(req)` | |
| `wings_ws_send_text(fd, s)` | `ws_send(fd, s)` | "text" varsayılan; kısalt |
| `wings_ws_send_close(fd)` | `ws_close(fd)` | |
| `wings_ws_send_pong(fd, s)` | `ws_pong(fd, s)` | |
| `wings_sse_headers()` | `sse_headers()` | |
| `wings_sse_event(n, d)` | `sse_event(n, d)` | |
| `wings_metrics_prom()` | `metrics_prom()` | zaten çoğunlukla iç kullanım |
| `enable_gzip(n)` | `gzip(n)` | kural 3; `gzip()` argümansız = makul default |
| `del(path, h)` | `delete(path, h)` (+ `del` kalır) | `delete` keyword değil — doğrulandı |
| `body_schema(s)` | `accepts(s)` | kural 6 |
| `response_model(s)` | `returns(s)` | kural 6 |
| `listen_pool(p, n)` | `serve(p, n)` | aşağıda |
| `session_attach(res, sid, secret)` | değişmez | dokunma — düşük trafik |

### Sunucu girişini tekle: `serve(port, workers)`

Default-arg padding (eksik arg → 0) bunu bugün mümkün kılıyor:

```tpr
serve();          // 8484 (Tulpar portu), tek thread   → listen(8484)
serve(8080);      // açık port                          → listen(8080)
serve(8080, 4);   // 4 worker'lı pool                   → listen_pool(8080, 4)
```

`listen/listen_pool/listen_evented/listen_async` kalır ama dokümanda
"Gelişmiş modlar" başlığına iner. Öğretilen tek kelime: **serve**.

### UFCS-first cheatsheet (docs'un yeni yüzü)

Kod değişikliği gerektirmeyen en ucuz kazanım: `req` okuyucularının tamamı
zaten UFCS'e uygun. Dokümantasyon ve örnekler şu stile geçer:

```tpr
func show(req) {
    int id    = req.param_int("id", 0);      // path parametresi
    str q     = req.query("q", "");          // query string
    int page  = req.query_int("page", 1);
    json body = req.json;                    // gövde (alan erişimi)
    str who   = req.form("name", "");        // form alanı
    json jar  = req.cookies();               // yeni cookies() ile
    ...
}
```

---

## 5. Motto testi — önce/sonra

| Senaryo | Bugün | Sonra |
|---|---|---|
| Kalıcı (SQLite) tam CRUD API + validasyon + docs | ~90 satır kod (`wings_notes_db.tpr`) | **7 satır** (`resource`) |
| Tek kayıt oku→güncelle handler'ı | ~15 satır (elle SQL + `sql_str` + `row_to_note`) | ~4 satır (`Note.find` / `Note.update`) |
| `done: 0` yazmak | **mümkün değil** (ORM bug) | çalışır |
| SQL injection'a karşı varsayılan | çağıranın disiplini | yapısal (param-only) |
| "Çerezleri nasıl okurum?" tahmini | `wings_cookies`?? | `req.cookies()` |
| "gzip'i nasıl açarım?" tahmini | `enable_gzip` | `gzip()` |

---

## 6. Uygulama planı

### Faz 1 — ORM v2 (saf Tulpar, düşük risk)
- `lib/orm.tpr` yeniden yazımı: `database/model/find/all/where/first/count/
  create/update/save/remove` + tip kısayolları + satır cast + param SQL.
- Eski `orm_*` isimleri sarmalayıcı olarak korunur.
- Yeni test: `tests/orm.test.tpr` (0/boş değer, injection, cast, çoklu db,
  save-upsert, eski isimler).
- ⚠️ `lib/*.tpr` değişince **`./build.sh clean`** (stale-embed bilinen tuzak)
  ve `cmake/EmbedLibraries.cmake` zaten `orm`'u gömüyor — slot değişikliği yok.

### Faz 2 — Wings isimleri + `resource()`
- Alias'lar: `cookies, ws_send/ws_close/ws_pong/ws_upgrade, sse_headers/
  sse_event, gzip, delete, accepts, returns, metrics_prom` (hepsi 1 satır).
- `serve(port, workers)` genişletmesi (workers>0 → `listen_pool`).
- `resource(path, Model, opts)` + şema türetme + OpenAPI beslemesi.
- `tests/wings_features.test.tpr`'a alias + resource vakaları.

### Faz 3 — Görünürlük (asıl "içgüdü" işi burada biter)
- `src/lsp/builtins.cpp`: yeni wings adları + **tüm ORM v2 sembolleri**
  (bugün ORM LSP'de hiç yok) → completion/hover.
- Örnek modernizasyonu: `wings_notes_db.tpr` → `resource()` sürümü (eski
  hali `examples/` altında "elle SQL" öğretici olarak kalabilir ama yanlış
  "parametreli sorgu YOK" yorumları düzeltilir); `api_wings_crud.tpr`'daki
  `_request` globali + string-handler stili modernize edilir; yeni
  `examples/wings_orm_resource.tpr`.
- Cheatsheet: tek sayfa kategorize API tablosu (UFCS stiliyle) — repo'da
  `WINGS_CHEATSHEET.md` + `tulpar-lang-web` docs'a taşıma.
- CHANGELOG + STATUS güncellemesi; README vitrine 7 satırlık örnek.

### Uygulamadan önce doğrulanacaklar
1. **Çift import dedup:** kullanıcı `import "orm"` + `import "wings"` yazar ve
   günün birinde wings orm'u import ederse tanımlar çift mi geliyor?
   (`wings_tls` → `wings` emsali tekil kullanımda çalışıyor; ikili durum test
   edilmeli. Bu yüzden §3'te wings→orm import'u *tercih edilmedi*.)
2. **`delete` fonksiyon adı** parser smoke testi (keyword değil ama `static`
   gibi çalıştığı bir örnekle kanıtlanmalı).
3. **Lambda handler:** `get("/x", (req) => ok({...}))` çalışıyor mu? Çalışırsa
   bonus (dokümante et); çalışmıyorsa bu turda kapsam dışı — zorlamaya değmez.
4. **UFCS zinciri yok:** `Note.find(1).title` gibi zincir receiver-identifier
   kısıtına takılabilir; API zaten zincirsiz tasarlandı, docs'ta tek satır not.
5. **Model handle globali + auto-persist etkileşimi:** `json Note = model(...)`
   global dict'ine auto-persist yazımlarının maliyeti/etkisi bir smoke ile
   kontrol edilmeli.

### Kapsam dışı (bilinçli erteleme)
- İlişkiler (`has_many`), migration sistemi, tipli query-builder
  (`Note.q().eq("done", 0)...`) — v2'nin çekmediği görülürse konuşulur.
- Inline lambda route'ları (doğrulama 3'e bağlı).
- `session_*`, `depends/dep`, `group` adları — bugünkü halleri kural setiyle
  çelişmiyor, dokunulmuyor.
