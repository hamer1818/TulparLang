# Plan 02 — Package Registry Tamamlaması

**Durum:** COMPLETED (2026-04 → 2026-05, PR'lar #47/#61/#71/#72/#82/#83/
\#229 + tulpar-be backend) — registry `api.pkg.tulparlang.dev` canlı,
CLI `init/list/add/remove/install/publish/search/info` + `.tpkg`
multi-file bundle + lockfile + full semver 2.0.0 ranges. Bkz. STATUS §
"Pkg ekosistemi".
**Tahmin:** 2-4 PR (1 client + 1 server + 1 entegrasyon + opsiyonel auth)
**Risk:** Düşük (mevcut altyapı %80 hazır)
**Mottoya katkı:** Ekosistem genişletme

## Hedef

`tulpar pkg install` `tulpar.toml` içindeki `[dependencies]` blok'undaki
non-`path:` spec'leri (örn. `http_client = "1.0"`) registry üzerinden
çekip `tulpar_modules/<name>/<name>.tpr` altına yazsın. Registry tarafı
(`pkg.tulparlang.dev` Astro/Cloudflare Pages projesi) versiyon resolve
eden Pages Function ile bytes serving yapsın.

## Mevcut durum (kaynak)

### Client tarafı — neredeyse tamam

- [src/pkg/manifest.cpp:88-91, 125-146](../src/pkg/manifest.cpp#L88-L146)
  TOML parser üç bölümü okuyor: top-level meta, `[registry] url = "..."`,
  `[dependencies]`.
- [src/pkg/manifest.hpp:28-42](../src/pkg/manifest.hpp#L28-L42) Manifest
  struct: `registry_url` alanı zaten var.
- [src/pkg/pkg_cli.cpp:216-268](../src/pkg/pkg_cli.cpp#L216-L268)
  Registry fetch yolu **uygulanmış**:
  - URL kalıbı: `<registry_url>/<name>/<spec>.tpr` (line 225-231)
  - HTTP fetch: `tulpar::http_fetch_url(url, body, status, err)`
    (line 235-241). TLS desteği `TULPAR_HAS_TLS` ile gating
    (PR #41'den sonra `https://` çalışıyor).
  - Yazma hedefi: `tulpar_modules/<name>/<name>.tpr` (line 248-265)
- [src/pkg/pkg_cli.cpp:216-224](../src/pkg/pkg_cli.cpp#L216-L224)
  `registry_url` boşsa "no registry configured" mesajı atlıyor.

### Eksik client davranışları

1. **Lockfile yok.** Aynı projeyi iki farklı makinede `install` farklı
   versiyon çekebilir (registry'de upstream güncellenmişse). `tulpar.lock`
   dosyası yok.
2. **Versiyon spec sınırlı.** `spec` doğrudan URL'e konuyor — yani spec
   `"1.0"` ise URL `/.../1.0.tpr`, `"^1.0"` ise URL `/.../^1.0.tpr`
   olur. Semver range resolve yok; resolver server-side olmalı.
3. **Cache yok.** Her `install` yeni indirir.
4. **Checksum verification yok.** Registry'den gelen byte'lar direkt
   diske yazılıyor, integrity check yok.
5. **Multi-file paket yok.** Tek `.tpr` dosyası iniyor; bir paketin
   multiple `.tpr` + altta `tulpar_modules/<name>/lib/...` yapısı yok.

### Server tarafı — Astro skeleton var, fonksiyon yok

- [d:/yazilim/pkg.tulparlang.dev](../../pkg.tulparlang.dev) Astro 6.2.1
  + Cloudflare Pages projesi. `package.json` build/dev/deploy script'leri
  hazır.
- README'de mimari taslağı var (lines 67-93): static UI listesi +
  `functions/[name]/[version].ts` Pages Function proxy + `src/content/
  packages/*.md` markdown manifest'leri + `/api/registry.json`
  build-time index.
- **Pages Function dosyası şu an yok** — registry endpoint canlı değil.

### Update komutuyla karşılaştırma

[src/cli/update_cmd.cpp:60-89](../src/cli/update_cmd.cpp#L60-L89) zaten
GitHub releases'tan binary çekiyor. Yöntem farklı: `update_cmd` platform
native curl/PowerShell çağırıyor; `pkg install` runtime'ın
`http_fetch_url`'ünü kullanıyor. İkisini birleştirmek mantıklı görünebilir
ama gerek yok — mevcut yol PR #41'den sonra TLS-ready.

## Yaklaşım: Üç bağımsız PR + opsiyonel auth

### PR 1 — Server: Pages Function + registry index

**Dosyalar:**
- `pkg.tulparlang.dev/functions/[name]/[version].ts` — yeni
- `pkg.tulparlang.dev/src/content/packages/<name>.md` — örnek 1-2 paket
- `pkg.tulparlang.dev/src/content/config.ts` — collection schema
- `pkg.tulparlang.dev/scripts/build-registry-index.mjs` — build-time
  `/api/registry.json` üretici (varsa Astro content collections yeterli)

**Function davranışı** (`functions/[name]/[version].ts`):

```ts
export const onRequestGet: PagesFunction = async ({ params, env }) => {
  const { name, version } = params;
  const meta = await loadPackageMeta(env, String(name));
  if (!meta) return new Response("not found", { status: 404 });
  const resolved = resolveSemver(meta.versions, String(version));
  if (!resolved) return new Response("version not found", { status: 404 });
  const assetUrl = meta.versions[resolved].asset;
  const upstream = await fetch(assetUrl);
  return new Response(upstream.body, {
    status: 200,
    headers: { "content-type": "text/plain; charset=utf-8",
               "x-tulpar-resolved-version": resolved,
               "x-tulpar-checksum-sha256": meta.versions[resolved].sha256 },
  });
};
```

**Önemli:** Cloudflare Pages Functions stream-through. Tulpar client'in
HTTP redirect destekleme zorunluluğu YOK (http_fetch_url 30x
takip etmiyor — README'de notlu). Bu yüzden GitHub release URL'ine
**redirect değil**, Function'ın kendisi proxy olmalı.

**Markdown manifest formatı:**
```yaml
---
name: http_client
description: HTTP client utility
versions:
  "1.0.0":
    asset: https://github.com/.../release/v1.0.0/http_client.tpr
    sha256: abc123...
  "1.1.0":
    asset: ...
    sha256: ...
---
# http_client
... readme markdown ...
```

`/api/registry.json` build-time'da tüm markdown dosyalarından üretilen
flat index — listeleme sayfası ve `tulpar pkg search` (gelecek) için.

### PR 2 — Client: lockfile + checksum

**Dosyalar:**
- `src/pkg/lockfile.cpp/.hpp` — yeni
- `src/pkg/pkg_cli.cpp` — `cmd_install` lockfile kontrol/yazma
- `src/pkg/manifest.hpp` — `Manifest::lock` tutar mı? Hayır, ayrı struct.

**Lockfile formatı (`tulpar.lock`):**
```toml
# Auto-generated; do not edit by hand.
[lock]
version = 1

[[package]]
name = "http_client"
spec_in_manifest = "^1.0"
resolved_version = "1.1.0"
sha256 = "abc123..."
source = "https://pkg.tulparlang.dev/http_client/1.1.0.tpr"
```

**Davranış:**
- `install` çalıştığında lock varsa → lock'taki resolved_version'ı çek,
  sha256 doğrula. Mismatch → fail.
- Lock yoksa veya `--update` flag'iyle çalıştırıldıysa → registry'den
  resolve, indir, doğrula, lock'a yaz.
- Lock dosyası `tulpar.toml` ile aynı dizinde; commit'lenir.

**Server response header'ından parse:**
- `x-tulpar-resolved-version` → lockfile'a yazılır
- `x-tulpar-checksum-sha256` → indirilen byte'ların SHA256'si ile
  karşılaştırılır

SHA256 implementasyonu: vendored mini SHA256 (40-50 satır C) —
external lib eklemekten kaçın.

### PR 3 — Multi-file paket desteği (opsiyonel)

Şu an URL `<reg>/<name>/<version>.tpr` ile tek dosya çekiyor. Multi-file
için `<reg>/<name>/<version>.tar.gz` veya `.zip` formatı:

**Server tarafı:** Function `Accept: application/x-tar` header'ına bakıp
arşiv döndürebilir. Astro Pages Function'da tar/zip oluşturmak ek iş;
basit yaklaşım — paket markdown'ı `bundle:` URL'i de listesin (release
asset olarak hazır .tar.gz).

**Client tarafı:** Mini tar/zip okuyucu — embedded zlib + minimal tar
parser. **Yüklü iş** (~500 satır C). Belki PR 3 yerine konvansiyon:
çok dosyalı paket → upstream `<reg>/<name>/<version>/<filename>.tpr`
formatında server'a koysun, client tek dosya çeker, `entry.tpr` ana
giriş dosyası olsun. Bu konvansiyonu **manifest formatına bir alan**
olarak koy:

```toml
[dependencies]
http_client = { version = "1.0", entry = "main.tpr" }
```

**Karar:** PR 3'ü ertele. Tek dosya yeterli ilk versiyon için; tek
dosyada yetmeyenler `path:` ile vendoring'e devam eder.

### PR 4 — Auth + publish komutu (opsiyonel, çok sonra)

`tulpar pkg publish` ile registry'ye paket yükleme. Token tabanlı auth.
Bu yazıldığı sırada Cloudflare Pages Functions tek başına yazma
yapamıyor — Workers KV / R2 / GitHub commit auth gerekir. Üç yol:

1. **GitHub-based:** Publish PR oluşturuyor → CI maintainer review →
   merge sonrası registry deploy ediyor. Manuel ama düşük altyapı.
2. **R2 + Worker auth:** Tam otomatik publish; daha karmaşık.
3. **Manuel/whitelist:** İlk fazda her paket maintainer ekibi
   tarafından eklenir. Zaman kazandırır, ekosistem büyüyene kadar yeter.

**Önerim:** Opsiyon 3 ile başla. PR 4'ü "ekosistem 10+ pakete ulaşınca"
diye ertele.

## Doğrulama

### Server tarafı

- Local dev: `npm run dev` (Astro + Wrangler emulation). Function
  endpoint'i `curl http://localhost:4321/http_client/1.0.0` ile test.
- Beklenen: 200 + body + 2 custom header.
- 404 path'leri: olmayan paket adı, olmayan versiyon.

### Client tarafı

- **Yeni test:** `tests/pkg/`. `tulpar.toml` fixture'ları ile end-to-end:
  - Geçerli paket → çekilir, lock yazılır
  - SHA256 mismatch → fail
  - Lock var, registry erişilebilir değil → lock'taki dosya hâlâ
    `tulpar_modules/`'da varsa kullan; yoksa fail
- **Manuel:** Gerçek registry'ye karşı `tulpar pkg install` çalıştır,
  `tulpar_modules/` ve `tulpar.lock` üretildiğini doğrula.

### Entegrasyon

- `examples/` altına bir paket-kullanan örnek: `examples/17_pkg_demo.tpr`
  → vendored bir `tulpar_modules/demo/demo.tpr` ile birlikte commit'le
  (CI'da gerçek registry call'ı yapma).
- README'de `tulpar pkg add http_client` komutunun çalıştığı bir
  quickstart bölümü.

## Açık sorular

- **Versioning policy:** semver mi, calver mi? `1.0.0` mı, `2026-05-05`
  mi? Resolve algoritması bu seçime bağlı. Önerim: semver (`^`, `~`,
  `>=`, exact).
- **Registry URL default'u:** `tulpar.toml` `[registry] url`'siz
  bir paket eklenince fallback `pkg.tulparlang.dev` olsun mu? Yoksa
  her zaman explicit mi olsun? Önerim: tulpar.toml `init` komutu
  default'u yazar (`https://pkg.tulparlang.dev`); kullanıcı silebilir.
- **Forks/mirrors:** Birden fazla registry destekleyelim mi? Faz 1 için
  HAYIR — `[registry] url` tek string. İleride array yapılabilir.
- **Yansıma (`tulpar pkg search`, `tulpar pkg info`):** PR 2'den sonra
  ayrı bir PR ile gelir; `/api/registry.json` indeksi zaten hazır
  oluyor.
- **`tulpar pkg add` davranışı:** Şu an
  ([src/pkg/pkg_cli.cpp](../src/pkg/pkg_cli.cpp)) sadece `tulpar.toml`'a
  satır ekliyor mu, yoksa indirip install da ediyor mu? Kontrol edip
  bu plana ek bir alt madde olarak hizala.
