# Session Handoff — 2026-07-02

> ⚠ **TARİHSEL NOT (2026-09-28).** Bu dosya 2026-07-02'nin devir notudur ve
> bütünüyle o günü anlatır; güncel durum `STATUS.md`, açık işler
> `plans/09_eksik_envanteri.md`. Bayatlayanlar: §1 "multi-file tarballs"
> (`.tpkg` çok dosyalı paket geldi; yalnız tulpar-be'nin GitHub yayın yolu tek
> dosya çekiyor — K246), §2 WebSocket çok çerçeve "Windows kutusu gerekir"
> (Windows CI 2026-09-21'den beri var), §4.1 v3.6.0 commit/push (yapıldı),
> §4.2 `wings_jwt` yayını (2026-07-04'te canlı), §4.4 JetBrains (2026-07-04
> kararı: YAPILMAYACAK).

Pick-up notes for the next session. Goal: resume efficiently (low token cost).
Cross-session project context also lives in Claude's memory
(`pkg-registry-rearchitecture.md`, `tulpar-v330-security-apis.md`, etc.).

---

## 1. Registry (unchanged since 2026-06-30 — LIVE in production)

The package-registry re-architecture (Faz 1 + Faz 2) is complete and deployed:
register (email/GitHub) → owner approves → self-serve GitHub publish → owner
approves package → installable with `tulpar pkg add`. Repos: `Tulpar`
(compiler), `tulpar-be` (backend, `api.pkg.tulparlang.dev`),
`pkg.tulparlang.dev` (frontend), `tulpar-lang-web` (docs). Each has its own
CLAUDE.md. Deploy cheatsheet: see §3.

Registry still-open (low priority): private-repo publishing, multi-file
tarballs, package signing, server-side /v1/search (N>50).

## 2. Wings completeness — ✅ DONE this session (v3.6.0, 2026-07-02)

The entire P1/P2/P3 gap list from the previous handoff was closed in one
round. Everything lives in `lib/wings.tpr` + one new C builtin:

- **P1:** `set_cookie`/`delete_cookie`, `set_signed_cookie`/`get_signed_cookie`
  (HMAC name-bound), sessions (`session_start/attach/get/set/destroy`),
  `cors(origin, {credentials,…})`, `rate_limit(max, window)` → 429.
- **P2:** `enable_gzip(min_bytes)` on top of new **`gzip_compress` builtin**
  (in-tree DEFLATE at `runtime/tulpar_gzip.cpp` — NO zlib/miniz dependency,
  deliberate: keeps AOT user binaries self-contained; verified byte-exact via
  Python gzip + curl --compressed). ETag→304 on `cached_get`.
- **P3:** `param`/`param_int`/`param_bool`, `jwt_guard(secret)`+`jwt_public`
  (wire-interop with wings_jwt package, claims → `req["jwt"]`),
  `html()`/`render()` templates, OpenAPI response schemas + bearerAuth.
- Side fix: `_wings_filter_obj` now preserves `_headers` (response_model no
  longer drops Set-Cookie/Location).
- Tests: `tests/wings_features.test.tpr` 13/13; live demo
  `examples/wings_features_api.tpr` (in COMPILE_ONLY_TESTS; every endpoint
  curl-verified). Full regression green: all examples + 33 focused suites.
- Version bumped to 3.6.0 (CMakeLists + CHANGELOG + STATUS.md + banner).

### ~~The ONE wings item still open~~ — closed by measurement (2026-09-27)
- **WebSocket recv multi-frame** — was a MinGW64/Windows-only calling-conv
  report that "needed a Windows box". Windows CI has existed since
  2026-09-21; `tests/ws_masked_client_smoke.py` now sends five masked frames
  on ONE connection (short, 16-bit length, two coalesced in one send,
  trailing) and the `build-windows` job runs it in its own step: **PASS on
  MinGW64** (and Linux, macOS). The quirk does not reproduce on the current
  tree; no code change was needed. The gate keeps it from coming back.

## 3. Quick deploy cheatsheet

- **SSH:** `ssh root@185.169.180.27` (deploy work only). Backend dir
  `/root/tulpar-be`; DB+blobs volume `/data`. Back up before schema changes:
  `docker cp tulpar-be:/data /root/tulpar-be-backups/data-<ts>`.
- **Backend deploy:** push `tulpar-be` `main` → Actions auto-deploy. If
  `TULPAR_REF` is moved to pick up v3.6.0 → full recompile (~10-15 min).
  New env vars go in BOTH server `.env` AND docker-compose `environment:`.
- **Frontend:** push `pkg.tulparlang.dev` `master` → CF Workers Build.
  `git check-ignore` new files. **Docs:** push `tulpar-lang-web` `master` →
  Vercel.
- Probes: `api.pkg.tulparlang.dev/healthz`, `/v1/packages`, `/v1/auth/me`.
- Outward-facing actions (deploy, publish, secrets) → confirm with user first.

## 4. Suggested next moves

1. **Commit/push this round** (user's call): compiler repo has the whole
   v3.6.0 round uncommitted. After push, optionally move `TULPAR_REF` in
   tulpar-be so the registry backend gets `cors()`/`rate_limit()` and can
   drop its hand-rolled limiter + same-origin proxy workaround.
2. **wings_jwt live publish** — still pending (needs user's bearer token):
   `cd packages/wings_jwt && tulpar pkg publish --token <TOKEN>`.
3. **Docs site** (`tulpar-lang-web`): add the v3.6.0 wings features to the
   Wings guide/cookbook (signed cookies + sessions + cors + rate-limit +
   jwt_guard + gzip + ETag + param helpers + html/render). The code-side
   docstrings in `lib/wings.tpr` are the source of truth.
4. STATUS.md "Açık eksikler"in kalanları: generics (🟢), HTTP/2 (🟢),
   JetBrains (🟢), registry content (🟡 — publish unblocks it).
