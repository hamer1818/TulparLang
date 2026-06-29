# wings_jwt

HMAC-SHA256 (HS256) signed tokens for [TulparLang](https://tulparlang.dev)
wings apps. Issue stateless session/auth tokens, verify them, enforce
expiry, and pull the bearer token off a request — with **zero
dependencies** (built on the runtime's `hmac_sha256` + `base64_encode`).

## Install

```bash
tulpar pkg add wings_jwt
```

## Use

```tulpar
import "wings";
import "wings_jwt" as jwt;

str SECRET = "change-me-in-prod";

func login(req) {
    // ... verify username/password (see password_hash/password_verify) ...
    str token = jwt.sign_ttl({"sub": "42", "role": "admin"}, SECRET, 3600);
    return ok({"token": token});
}

func me(req) {
    json v = jwt.verify(jwt.from_header(req), SECRET);
    if (v["ok"] == 0) { return unauthorized({"error": v["error"]}); }
    return ok({"user": v["claims"]["sub"], "role": v["claims"]["role"]});
}

func main() {
    post("/login", login);
    get("/me", me);
    serve();
}
```

## API

| Function | Returns |
|---|---|
| `sign(claims, secret)` | compact `header.payload.signature` token |
| `sign_ttl(claims, secret, ttl_seconds)` | token with `iat` + `exp` stamped |
| `verify(token, secret)` | `{"ok":1,"claims":{…}}` or `{"ok":0,"error":"…"}` |
| `decode(token)` | claims **without** verifying (inspection only) |
| `from_header(req)` | bearer token string, or `""` |

`verify()` error reasons: `"malformed token"`, `"bad signature"`, `"expired"`.

## Token format

```
base64url({"alg":"HS256","typ":"JWT"})
. base64url(<claims JSON>)
. base64url(hmac_sha256(secret, "<header>.<payload>"))
```

A standard JWT structure with base64url-encoded JSON segments, signed
with HS256.

**Note — Tulpar-native variant:** the signature segment encodes the
*hex* HMAC digest (not the raw MAC bytes), so tokens are self-issued and
self-verified by Tulpar services. Wire-interop with external RFC-7519
validators (which base64url the raw MAC) is on the roadmap; it needs a
raw-bytes base64 path in the runtime.

## Security notes

- Use a long, random `secret` (≥ 32 bytes). `secure_token(48)` is a good
  source.
- `verify()` checks `exp` automatically when present; always set a TTL.
- Signature comparison is currently not constant-time (planned). The HMAC
  itself is computed in the runtime; the practical timing surface over a
  network is small, but treat this as a v0.1 limitation.
- `decode()` does **not** verify — never trust its claims for auth.

## Requirements

Requires a TulparLang runtime with the `hmac_sha256` builtin (v3.5.0+).

## License

MIT
