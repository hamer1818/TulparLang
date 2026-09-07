// Ag yuzeyi: HTTP istemcisi (senkron + async) ve TLS sunucu primitifleri.
//
// NEDEN AYRI BIR DERLEME BIRIMI: bu kodun tamami OpenSSL'e bagli, ve
// `runtime_bindings.cpp` HER AOT ikilisine giriyor. Ikisi ayni nesnede
// oldugu surece `print(1)` yazan bir program bile libssl + libcrypto (ve
// onlarin libz/brotli/zstd bagimliliklarini) YUKLUYORDU. Olculdu
// (2026-09-06, pinlenmis): bos bir C++ programi 0,72 ms, OpenSSL
// baglandiginda 1,00 ms — her Tulpar programina 0,28 ms sabit vergi.
//
// Ayri bir birimde durunca bagliyici bu nesneyi YALNIZ program gercekten
// bir http/tls yerlesigi cagirdiginda iceri aliyor; `-Wl,--as-needed`
// de kullanilmayan libssl/libcrypto'yu link satirindan dusuruyor.
//
// Buraya OpenSSL'e dokunan her yeni yerlesik gelmeli. Ayni sey SQLite
// icin de gecerli — o ayrimi henuz yapmadik (bkz. Performance.md).
#include "../../runtime/cJSON.h"
#include "../common/localization.hpp"
#include "../common/platform.h"
#include "../common/platform_sockets.h"
#include "../common/platform_threads.h"
#include "vm.hpp"
#include "runtime_http_obj.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

// `runtime_bindings.cpp` ile ayni soket takma adlari (orada da ayni
// bicimde tanimli; platform_sockets.h ham `socket_t` veriyor).
#define tulpar_socket socket_t
#define tulpar_close tulpar_socket_close
#define tulpar_send send
#define tulpar_recv recv
#define tulpar_invalid_socket INVALID_SOCKET_VALUE

#ifdef __cplusplus
extern "C" {
#endif

// runtime_bindings.cpp'de tanimli arena yardimcilari.
ObjString *aot_allocate_string(const char *chars, int length);
void *aot_arena_alloc(size_t size);
// ---------------------------------------------------------------------------
// http_request(method, url, body) -> json (or VM_INT(0) on error)
//
// Outbound HTTP/1.0 client. Plain HTTP always; HTTPS supported when
// Tulpar is built with OpenSSL (`-DTULPAR_HAS_TLS=1`). Currently:
// no redirects, no chunked encoding, no keep-alive on the client
// side. Enough to hit local services, internal admin APIs, public
// JSON endpoints over HTTPS.
//
// Returns an object:
//   { "ok": 1, "status": <int>, "headers": <obj>, "body": <str> }
// or  { "ok": 0, "error": "<reason>" } on failure.
//
// Uses getaddrinfo so hostnames work (the existing socket_client uses
// inet_pton and only accepts IPs).
// ---------------------------------------------------------------------------

#if defined(_WIN32)
  // ws2tcpip.h is already pulled in by platform_sockets.h on Windows.
#else
  #include <netdb.h>
#endif

// Nesne kurucu ucluk `runtime_bindings.cpp`de tanimli (http_parse_request
// da kullaniyor); bildirimleri runtime_http_obj.hpp'den geliyor.

// Bridge into the namespaced http_fetch helpers. The `#include` lives
// outside any `extern "C"` block to keep C++ linkage on the namespace
// declarations; that's why we declare the prototype manually here.
} // close extern "C" before including the C++-namespaced header
#include "../common/http_fetch.hpp"
extern "C" {

namespace {

// Parse `http://host[:port]/path?query` into pieces. Stores results in
// `out_*`. Returns false on malformed input.
bool parse_http_url(const char *url, std::string &out_host, int &out_port,
                    std::string &out_path) {
    if (!url) return false;
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        // No TLS support. Fail loud.
        return false;
    } else {
        return false;  // require explicit scheme
    }

    const char *host_end = p;
    while (*host_end && *host_end != ':' && *host_end != '/') host_end++;
    out_host.assign(p, host_end - p);
    if (out_host.empty()) return false;

    out_port = 80;
    if (*host_end == ':') {
        const char *port_start = host_end + 1;
        const char *port_end = port_start;
        while (*port_end >= '0' && *port_end <= '9') port_end++;
        if (port_end == port_start) return false;
        out_port = atoi(port_start);
        host_end = port_end;
    }

    if (*host_end == '/') {
        out_path = host_end;
    } else if (*host_end == '\0') {
        out_path = "/";
    } else {
        return false;
    }
    return true;
}

tulpar_socket http_dial(const char *host, int port) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        return tulpar_invalid_socket;
    }
    tulpar_socket sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == tulpar_invalid_socket) {
        freeaddrinfo(res);
        return tulpar_invalid_socket;
    }
    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) < 0) {
        tulpar_close(sock);
        freeaddrinfo(res);
        return tulpar_invalid_socket;
    }
    freeaddrinfo(res);
    return sock;
}

}  // namespace

// Build a { ok:0, error } envelope. Shared by the blocking and async paths.
static VMValue aot_http_error_obj(const char *msg) {
    ObjObject *r = aot_http_make_obj(2);
    aot_http_obj_set(r, "ok", 2, VM_INT(0));
    aot_http_obj_set_str(r, "error", 5, msg, (int)strlen(msg));
    return VM_OBJ((Obj *)r);
}

// Parse a raw HTTP response buffer (status line + headers + body) into the
// { ok:1, status, headers, body } object. Shared by aot_http_request (sync)
// and the async completion path. MUST run on the main thread — it allocates
// VM objects/strings, which are not safe to touch from a worker thread.
static VMValue aot_http_build_response(const std::string &buf) {
    // Parse status line
    size_t line_end = buf.find('\n');
    if (line_end == std::string::npos) return aot_http_error_obj("malformed response");
    std::string status_line = buf.substr(0, line_end);
    if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();

    int status = 0;
    {
        size_t sp = status_line.find(' ');
        if (sp == std::string::npos) return aot_http_error_obj("malformed status line");
        size_t sp2 = status_line.find(' ', sp + 1);
        std::string code = status_line.substr(sp + 1,
            (sp2 == std::string::npos ? status_line.size() : sp2) - sp - 1);
        status = atoi(code.c_str());
    }

    // Parse headers
    ObjObject *headers = aot_http_make_obj(8);
    size_t pos = line_end + 1;
    while (pos < buf.size()) {
        size_t le = buf.find('\n', pos);
        if (le == std::string::npos) break;
        std::string h = buf.substr(pos, le - pos);
        if (!h.empty() && h.back() == '\r') h.pop_back();
        pos = le + 1;
        if (h.empty()) break;
        size_t colon = h.find(':');
        if (colon == std::string::npos) continue;
        std::string k = h.substr(0, colon);
        size_t v_start = colon + 1;
        while (v_start < h.size() && (h[v_start] == ' ' || h[v_start] == '\t'))
            v_start++;
        std::string v = h.substr(v_start);
        aot_http_obj_set_str(headers, k.data(), (int)k.size(),
                             v.data(), (int)v.size());
    }

    std::string body_str = pos < buf.size() ? buf.substr(pos) : std::string();

    ObjObject *out = aot_http_make_obj(4);
    aot_http_obj_set(out, "ok", 2, VM_INT(1));
    aot_http_obj_set(out, "status", 6, VM_INT(status));
    aot_http_obj_set(out, "headers", 7, VM_OBJ((Obj *)headers));
    aot_http_obj_set_str(out, "body", 4, body_str.data(), (int)body_str.size());
    return VM_OBJ((Obj *)out);
}

VMValue aot_http_request(VMValue methodVal, VMValue urlVal, VMValue bodyVal) {
    if (!IS_STRING(methodVal) || !IS_STRING(urlVal))
        return aot_http_error_obj("bad args");
    std::string method = AS_STRING(methodVal)->chars;
    std::string url = AS_STRING(urlVal)->chars;
    std::string body;
    if (IS_STRING(bodyVal)) {
        body.assign(AS_STRING(bodyVal)->chars, AS_STRING(bodyVal)->length);
    }

    // Delegate to the shared http_fetch implementation so HTTPS
    // (when TLS is compiled in) is supported uniformly with the
    // package manager's registry fetch.
    std::string buf, fetch_err;
    if (!tulpar::http_request_url(method, url, body, buf, fetch_err)) {
        return aot_http_error_obj(fetch_err.c_str());
    }
    return aot_http_build_response(buf);
}

// http_request(method, url, body, headers) -> json. Same as the 3-arg
// form, but `headers` is a {name: value} object appended verbatim to the
// request (after the auto Host/User-Agent/Content-Length). Needed for
// APIs that require an `Authorization` / `Accept` header (e.g. GitHub).
// Non-object `headers` (incl. a 0 from a 3-arg call padded to 4) → none.
// Header names/values containing CR/LF are dropped (no header injection).
VMValue aot_http_request_h(VMValue methodVal, VMValue urlVal, VMValue bodyVal,
                           VMValue headersVal) {
    if (!IS_STRING(methodVal) || !IS_STRING(urlVal))
        return aot_http_error_obj("bad args");
    std::string method = AS_STRING(methodVal)->chars;
    std::string url = AS_STRING(urlVal)->chars;
    std::string body;
    if (IS_STRING(bodyVal)) {
        body.assign(AS_STRING(bodyVal)->chars, AS_STRING(bodyVal)->length);
    }
    std::string extra;
    if (IS_OBJECT(headersVal)) {
        ObjObject *h = AS_OBJECT(headersVal);
        for (int i = 0; i < h->count; i++) {
            if (!h->keys[i]) continue;
            std::string nm(h->keys[i]->chars, (size_t)h->keys[i]->length);
            VMValue v = h->values[i];
            if (!IS_STRING(v)) continue; // only string header values
            std::string val(AS_STRING(v)->chars, (size_t)AS_STRING(v)->length);
            if (nm.find('\r') != std::string::npos || nm.find('\n') != std::string::npos)
                continue;
            if (val.find('\r') != std::string::npos || val.find('\n') != std::string::npos)
                continue;
            extra += nm;
            extra += ": ";
            extra += val;
            extra += "\r\n";
        }
    }
    std::string buf, fetch_err;
    if (!tulpar::http_request_url(method, url, body, buf, fetch_err, extra)) {
        return aot_http_error_obj(fetch_err.c_str());
    }
    return aot_http_build_response(buf);
}

// ---------------------------------------------------------------------------
// Async HTTP — http_request_async(method, url, body) -> promise<json>
//
// Offloads the blocking request to a tiny worker pool so the single-threaded
// async event loop keeps pumping other coroutines while the socket I/O is in
// flight. Worker threads only do the network leg (http_request_url, filling
// std::strings); the main thread parses the buffer into VM objects and settles
// the promise in http_async_poll(). Nothing on the worker side touches the
// scheduler, so the handoff needs only one atomic flag per job — no locking of
// the ready queue / timers. Pool size defaults to 4, override TULPAR_HTTP_POOL.
// ---------------------------------------------------------------------------
extern "C" {
ObjPromise *aot_promise_new(void);
void aot_promise_settle(ObjPromise *p, VMValue value, int state);
void aot_io_register(int (*poll)(void *ud), void *ud);
}

namespace {

struct HttpAsyncJob {
    std::string method, url, body; // input (owned copies, read by worker)
    std::string buf, err;          // output (written by worker)
    bool ok = false;
    std::atomic<int> done{0};      // 0 pending, 1 finished (release/acquire)
    ObjPromise *promise = nullptr;
};

tulpar_mutex_t g_http_pool_mtx;
std::vector<HttpAsyncJob *> g_http_pool_queue; // guarded by g_http_pool_mtx
bool g_http_pool_inited = false;

#if PLATFORM_WINDOWS
unsigned __stdcall http_pool_worker(void *) {
#else
void *http_pool_worker(void *) {
#endif
    for (;;) {
        HttpAsyncJob *job = nullptr;
        tulpar_mutex_lock(&g_http_pool_mtx);
        if (!g_http_pool_queue.empty()) {
            job = g_http_pool_queue.front();
            g_http_pool_queue.erase(g_http_pool_queue.begin());
        }
        tulpar_mutex_unlock(&g_http_pool_mtx);
        if (!job) {
            tulpar_thread_sleep(1); // idle: nothing queued
            continue;
        }
        job->ok = tulpar::http_request_url(job->method, job->url, job->body,
                                           job->buf, job->err);
        job->done.store(1, std::memory_order_release);
    }
#if PLATFORM_WINDOWS
    return 0;
#else
    return nullptr;
#endif
}

// Event-loop completion poll (main thread). Returns 1 once the worker has
// finished — having first built the response object and settled the promise —
// so the loop drops this source.
int http_async_poll(void *ud) {
    HttpAsyncJob *job = (HttpAsyncJob *)ud;
    if (job->done.load(std::memory_order_acquire) == 0) return 0;
    VMValue result = job->ok ? aot_http_build_response(job->buf)
                             : aot_http_error_obj(job->err.c_str());
    aot_promise_settle(job->promise, result, 1);
    delete job;
    return 1;
}

void http_pool_init() {
    if (g_http_pool_inited) return;
    g_http_pool_inited = true;
    tulpar_mutex_init(&g_http_pool_mtx);
    int n = 4;
    const char *env = getenv("TULPAR_HTTP_POOL");
    if (env && *env) {
        n = atoi(env);
        if (n < 1) n = 1;
        if (n > 64) n = 64;
    }
    for (int i = 0; i < n; i++) {
        tulpar_thread_t th;
        if (tulpar_thread_create(&th, (tulpar_thread_func_t)http_pool_worker,
                                 nullptr) == 0)
            tulpar_thread_detach(th);
    }
}

}  // namespace

VMValue aot_http_request_async(VMValue methodVal, VMValue urlVal,
                               VMValue bodyVal) {
    ObjPromise *p = aot_promise_new();
    if (!IS_STRING(methodVal) || !IS_STRING(urlVal)) {
        aot_promise_settle(p, aot_http_error_obj("bad args"), 1);
        return VM_OBJ((Obj *)p);
    }
    http_pool_init();

    HttpAsyncJob *job = new HttpAsyncJob();
    job->promise = p;
    job->method = AS_STRING(methodVal)->chars;
    job->url = AS_STRING(urlVal)->chars;
    if (IS_STRING(bodyVal))
        job->body.assign(AS_STRING(bodyVal)->chars, AS_STRING(bodyVal)->length);

    tulpar_mutex_lock(&g_http_pool_mtx);
    g_http_pool_queue.push_back(job);
    tulpar_mutex_unlock(&g_http_pool_mtx);

    aot_io_register(&http_async_poll, job);
    return VM_OBJ((Obj *)p);
}
// ============================================================================
// TLS server primitives — power Wings TLS listener (lib/wings_tls.tpr)
// ============================================================================
//
// Mirrors the client-side OpenSSL path in src/common/http_fetch.cpp but
// flips the direction: SSL_CTX uses TLS_server_method() + a cert/key
// pair the user supplies, every accepted fd gets wrapped in SSL_accept,
// and read/write go through SSL_read / SSL_write instead of recv / send.
//
// The Tulpar surface keeps the SSL pointer as an opaque int64 (raw
// reinterpret_cast of `SSL *`). User code holds it as `int ssl` and
// passes it to the four helpers; lifetime is tied to `tls_close`. We
// keep the SSL_CTX alive for the listener's whole lifetime — one
// per call to `tls_listen` — so cert/key files are read once at
// startup and shared across every connection that listener serves.
//
// Build: gated on TULPAR_HAS_TLS (set by CMake when find_package(OpenSSL)
// succeeds). Without TLS the builtins return error sentinels so calling
// code degrades gracefully — Tulpar binaries built on a host without
// OpenSSL still run, just without the TLS surface.

#if defined(TULPAR_HAS_TLS)
  #include <openssl/ssl.h>
  #include <openssl/err.h>
#endif

// Initialize a TLS server context. Loads the cert + key from disk,
// configures TLS_server_method, and returns the SSL_CTX pointer as
// int64 for Tulpar code to thread back into tls_accept. Returns 0 on
// any failure (invalid paths, mismatched cert/key, or no-TLS build).
VMValue aot_tls_init(VMValue certVal, VMValue keyVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_STRING(certVal) || !IS_STRING(keyVal)) return VM_INT(0);
  const char *cert_path = AS_STRING(certVal)->chars;
  const char *key_path = AS_STRING(keyVal)->chars;
  // OpenSSL 1.1.0+ initializes itself lazily; older versions need
  // OPENSSL_init_ssl(0, nullptr) but we already require modern OpenSSL
  // for the client-side TLS support.
  SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
  if (!ctx) return VM_INT(0);
  // Reasonable defaults: TLS 1.2+, no SSLv3 / TLS 1.0/1.1.
  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
  if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) != 1) {
    fprintf(stderr, "[tls] use_certificate_file failed for %s\n", cert_path);
    SSL_CTX_free(ctx);
    return VM_INT(0);
  }
  if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) != 1) {
    fprintf(stderr, "[tls] use_PrivateKey_file failed for %s\n", key_path);
    SSL_CTX_free(ctx);
    return VM_INT(0);
  }
  if (SSL_CTX_check_private_key(ctx) != 1) {
    fprintf(stderr, "[tls] check_private_key failed (cert/key mismatch?)\n");
    SSL_CTX_free(ctx);
    return VM_INT(0);
  }
  return VM_INT((int64_t)(uintptr_t)ctx);
#else
  (void)certVal; (void)keyVal;
  return VM_INT(0);
#endif
}

// Accept + SSL_accept on a connection that arrived on a plain TCP
// listening socket. The caller keeps using `socket_accept(server)` to
// get the raw fd, then hands the fd here along with the ctx returned
// from tls_init. We do the SSL_accept handshake synchronously and hand
// back the SSL* (cast to int64) on success, 0 on failure. On failure
// we close the underlying socket so the caller doesn't have to track
// it separately.
VMValue aot_tls_accept(VMValue ctxVal, VMValue clientFdVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(ctxVal) || !IS_INT(clientFdVal)) return VM_INT(0);
  SSL_CTX *ctx = (SSL_CTX *)(uintptr_t)AS_INT(ctxVal);
  tulpar_socket fd = (tulpar_socket)AS_INT(clientFdVal);
  if (!ctx) return VM_INT(0);
  SSL *ssl = SSL_new(ctx);
  if (!ssl) return VM_INT(0);
  if (SSL_set_fd(ssl, (int)fd) != 1) {
    SSL_free(ssl);
    return VM_INT(0);
  }
  int hs = SSL_accept(ssl);
  if (hs != 1) {
    int err = SSL_get_error(ssl, hs);
    fprintf(stderr, "[tls] SSL_accept failed err=%d\n", err);
    SSL_free(ssl);
    tulpar_close(fd);
    return VM_INT(0);
  }
  return VM_INT((int64_t)(uintptr_t)ssl);
#else
  (void)ctxVal; (void)clientFdVal;
  return VM_INT(0);
#endif
}

// Read up to `max_bytes` from a TLS connection. Returns the bytes as
// a Tulpar string. Empty string on EOF or error — same shape as
// `socket_receive` so wings handler logic can drive both sides with
// the same contract.
VMValue aot_tls_recv(VMValue sslVal, VMValue maxVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(sslVal) || !IS_INT(maxVal)) {
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  }
  SSL *ssl = (SSL *)(uintptr_t)AS_INT(sslVal);
  int max_bytes = (int)AS_INT(maxVal);
  if (!ssl || max_bytes <= 0 || max_bytes > 16 * 1024 * 1024) {
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  }
  char *buf = (char *)malloc((size_t)max_bytes + 1);
  if (!buf) return VM_OBJ((Obj *)aot_allocate_string("", 0));
  int n = SSL_read(ssl, buf, max_bytes);
  if (n <= 0) {
    free(buf);
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  }
  buf[n] = 0;
  ObjString *res = aot_allocate_string(buf, n);
  free(buf);
  return VM_OBJ((Obj *)res);
#else
  (void)sslVal; (void)maxVal;
  return VM_OBJ((Obj *)aot_allocate_string("", 0));
#endif
}

// Write a Tulpar string to a TLS connection. Returns the byte count
// SSL_write reported, or -1 on error / closed connection. Single-shot;
// no partial-write retry, mirroring the existing socket_send shape.
// Wings' typical < 64 KiB JSON responses fit inside a single SSL_write.
VMValue aot_tls_send(VMValue sslVal, VMValue dataVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(sslVal) || !IS_STRING(dataVal)) return VM_INT(-1);
  SSL *ssl = (SSL *)(uintptr_t)AS_INT(sslVal);
  ObjString *s = AS_STRING(dataVal);
  if (!ssl || !s) return VM_INT(-1);
  int n = SSL_write(ssl, s->chars, (int)s->length);
  return VM_INT((int64_t)n);
#else
  (void)sslVal; (void)dataVal;
  return VM_INT(-1);
#endif
}

// Tear down an SSL connection and close the underlying TCP fd. Idempotent
// on a NULL pointer — handler code that hits an early SSL_accept failure
// and calls tls_close(0) shouldn't crash.
VMValue aot_tls_close(VMValue sslVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(sslVal)) return VM_INT(0);
  SSL *ssl = (SSL *)(uintptr_t)AS_INT(sslVal);
  if (!ssl) return VM_INT(0);
  // Best-effort graceful shutdown — clients that already closed don't
  // need a response, and SSL_shutdown returning 0 is normal in that case.
  SSL_shutdown(ssl);
  int fd = SSL_get_fd(ssl);
  SSL_free(ssl);
  if (fd >= 0) tulpar_close((tulpar_socket)fd);
  return VM_INT(0);
#else
  (void)sslVal;
  return VM_INT(0);
#endif
}

// Free an SSL_CTX returned by tls_init. Call once per listener at
// shutdown — typically never in practice because Wings servers run
// to SIGTERM, but exposed so embed-style use can clean up explicitly.
VMValue aot_tls_ctx_free(VMValue ctxVal) {
#if defined(TULPAR_HAS_TLS)
  if (!IS_INT(ctxVal)) return VM_INT(0);
  SSL_CTX *ctx = (SSL_CTX *)(uintptr_t)AS_INT(ctxVal);
  if (ctx) SSL_CTX_free(ctx);
  return VM_INT(0);
#else
  (void)ctxVal;
  return VM_INT(0);
#endif
}

#ifdef __cplusplus
}
#endif
