# Test fixtures

Files in this directory are pre-generated for the CI smoke suite. They
are NEVER used for production, NEVER reflect real keys, and the names
are stable so the smoke runner can refer to them by path.

## `tls_dev.crt` + `tls_dev.key`

Self-signed RSA-2048 certificate for `CN=localhost` (subjectAltName
includes `IP:127.0.0.1`). Valid for 10 years from generation
(2026-05 → 2036-05). Used by `examples/api_wings_tls.tpr` and its
smoke probe to exercise the `wings_tls` HTTPS listener end-to-end on
CI without requiring runtime cert generation.

The key is intentionally checked in — it has no real-world trust
value (no CA signature, no real domain) and is hard-coded to expire
well before any production system would consider it valid.

Regenerate with:

```bash
MSYS2_ARG_CONV_EXCL='*' openssl req -x509 -newkey rsa:2048 \
    -keyout tests/fixtures/tls_dev.key \
    -out tests/fixtures/tls_dev.crt \
    -days 3650 -nodes \
    -subj "/CN=localhost/O=Tulpar Test Fixtures/OU=DO NOT USE IN PRODUCTION" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

(The `MSYS2_ARG_CONV_EXCL='*'` prefix only matters on Git-Bash /
MSYS2 shells, where it stops the `/CN=...` path-style arg from
getting auto-translated to a Windows path.)
