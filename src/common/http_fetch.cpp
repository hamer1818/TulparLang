#include "http_fetch.hpp"
#include "platform_sockets.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
  // ws2tcpip.h is included via platform_sockets.h.
#else
  #include <netdb.h>
#endif

#if defined(TULPAR_HAS_TLS)
  #include <openssl/ssl.h>
  #include <openssl/err.h>
  #include <openssl/x509v3.h>
#endif

#if !defined(_WIN32)
  #include <sys/types.h>  // ssize_t
#endif

// Local socket-API alias macros (mirror what runtime_bindings.cpp uses
// internally — keeping them local rather than promoting them avoids
// touching every other call site).
#define tul_socket   socket_t
#define tul_close    tulpar_socket_close
#define tul_send     send
#define tul_recv     recv
#define TUL_INVALID  INVALID_SOCKET_VALUE
#if defined(_WIN32)
  typedef long long ssize_local_t;
#else
  typedef ssize_t  ssize_local_t;
#endif

namespace tulpar {

namespace {

#if defined(TULPAR_HAS_TLS)
// Configure a fresh SSL_CTX with the same trust-store rules used by
// every TLS client in the codebase (http_fetch_url + http_request_url
// today; future tls_* builtins too). Rules:
//
//   1. Default: SSL_VERIFY_PEER + hostname check. Calls
//      SSL_CTX_set_default_verify_paths() first so distro-installed
//      CA bundles work out of the box on Linux/macOS.
//   2. If $TULPAR_CA_BUNDLE points at a readable PEM file, load it on
//      top of the defaults. This is how MSYS2 builds (and CI) point
//      at the system trust store explicitly.
//   3. If $TULPAR_TLS_INSECURE=1, downgrade to SSL_VERIFY_NONE. Dev
//      and self-signed-fixture escape hatch; never the default.
//
// Returns the configured SSL_CTX or nullptr on failure (caller frees
// with SSL_CTX_free).
SSL_CTX *make_client_tls_ctx(std::string &out_err) {
    static bool g_ssl_inited = false;
    if (!g_ssl_inited) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        g_ssl_inited = true;
    }
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        out_err = "SSL_CTX_new failed";
        return nullptr;
    }
    SSL_CTX_set_default_verify_paths(ctx);
    const char *ca_bundle = std::getenv("TULPAR_CA_BUNDLE");
    if (ca_bundle && ca_bundle[0]) {
        if (SSL_CTX_load_verify_locations(ctx, ca_bundle, nullptr) != 1) {
            SSL_CTX_free(ctx);
            out_err = "TULPAR_CA_BUNDLE points at unreadable file: ";
            out_err += ca_bundle;
            return nullptr;
        }
    }
    const char *insecure = std::getenv("TULPAR_TLS_INSECURE");
    if (insecure && insecure[0] == '1') {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
    }
    return ctx;
}

// Wire up SNI + hostname verification on a fresh SSL object. Mirrors
// what every modern client (curl, libcurl, requests) does after
// SSL_new + before SSL_connect. Skipped when TULPAR_TLS_INSECURE=1 is
// set — make_client_tls_ctx has already disabled chain verification
// in that mode and the hostname check would otherwise still fail on
// self-signed dev fixtures.
void apply_tls_hostname_check(SSL *ssl, const std::string &host) {
    SSL_set_tlsext_host_name(ssl, host.c_str());
    const char *insecure = std::getenv("TULPAR_TLS_INSECURE");
    if (insecure && insecure[0] == '1') return;
    X509_VERIFY_PARAM *param = SSL_get0_param(ssl);
    if (!param) return;
    X509_VERIFY_PARAM_set_hostflags(
        param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    X509_VERIFY_PARAM_set1_host(param, host.c_str(), host.size());
}
#endif  // TULPAR_HAS_TLS

bool parse_http_url_internal(const std::string &url, std::string &host,
                             int &port, std::string &path, bool &is_https) {
    const char *p = url.c_str();
    if (std::strncmp(p, "http://", 7) == 0) {
        p += 7;
        is_https = false;
    } else if (std::strncmp(p, "https://", 8) == 0) {
        p += 8;
        is_https = true;
    } else {
        return false;
    }
    const char *end = p;
    while (*end && *end != ':' && *end != '/') end++;
    host.assign(p, end - p);
    if (host.empty()) return false;
    port = is_https ? 443 : 80;
    if (*end == ':') {
        const char *ps = end + 1;
        const char *pe = ps;
        while (*pe >= '0' && *pe <= '9') pe++;
        if (pe == ps) return false;
        port = std::atoi(ps);
        end = pe;
    }
    if (*end == '/') {
        path = end;
    } else if (*end == '\0') {
        path = "/";
    } else {
        return false;
    }
    return true;
}

}  // namespace

bool http_fetch_url(const std::string &url, std::string &out_body,
                    int &out_status, std::string &out_err) {
    out_body.clear();
    out_status = 0;
    out_err.clear();

    std::string host, path;
    int port;
    bool is_https = false;
    if (!parse_http_url_internal(url, host, port, path, is_https)) {
        out_err = "bad url (use http:// or https://)";
        return false;
    }
#if !defined(TULPAR_HAS_TLS)
    if (is_https) {
        out_err = "TLS not compiled in (build Tulpar with OpenSSL to enable https://)";
        return false;
    }
#endif

#if defined(_WIN32)
    // The runtime's aot_runtime_init normally calls WSAStartup; pkg_cli
    // runs before any of that. Initialise on demand here so the
    // CLI-only callers don't need to know about Winsock.
    static bool g_wsa_started = false;
    if (!g_wsa_started) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
        g_wsa_started = true;
    }
#endif

    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), port_str, &hints, &res) != 0 || !res) {
        out_err = "dns failed for " + host;
        return false;
    }
    tul_socket sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == TUL_INVALID) {
        freeaddrinfo(res);
        out_err = "socket() failed";
        return false;
    }
    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) < 0) {
        tul_close(sock);
        freeaddrinfo(res);
        out_err = "connect to " + host + " failed";
        return false;
    }
    freeaddrinfo(res);

    std::string req;
    req += "GET ";
    req += path;
    req += " HTTP/1.0\r\nHost: ";
    req += host;
    if (port != 80) {
        char pb[16]; std::snprintf(pb, sizeof(pb), ":%d", port);
        req += pb;
    }
    req += "\r\nUser-Agent: tulpar-pkg/0.2\r\nAccept: */*\r\nConnection: close\r\n\r\n";

    std::string buf;
    buf.reserve(4096);

#if defined(TULPAR_HAS_TLS)
    if (is_https) {
        SSL_CTX *ctx = make_client_tls_ctx(out_err);
        if (!ctx) {
            tul_close(sock);
            return false;
        }

        SSL *ssl = SSL_new(ctx);
        if (!ssl) {
            SSL_CTX_free(ctx);
            tul_close(sock);
            out_err = "SSL_new failed";
            return false;
        }
        apply_tls_hostname_check(ssl, host);
        SSL_set_fd(ssl, (int)sock);
        if (SSL_connect(ssl) != 1) {
            unsigned long ssl_err = ERR_peek_last_error();
            int verify_rc = (int)SSL_get_verify_result(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            tul_close(sock);
            char detail[160];
            if (verify_rc != X509_V_OK) {
                std::snprintf(detail, sizeof(detail),
                              " (cert verify: %s)",
                              X509_verify_cert_error_string(verify_rc));
            } else if (ssl_err) {
                std::snprintf(detail, sizeof(detail), " (ssl: 0x%lx)", ssl_err);
            } else {
                detail[0] = '\0';
            }
            out_err = "TLS handshake failed for " + host + detail;
            return false;
        }
        if (SSL_write(ssl, req.data(), (int)req.size()) <= 0) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            tul_close(sock);
            out_err = "TLS send failed";
            return false;
        }
        char chunk[4096];
        for (;;) {
            int n = SSL_read(ssl, chunk, sizeof(chunk));
            if (n <= 0) break;
            buf.append(chunk, n);
            if (buf.size() > 64 * 1024 * 1024) break;
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        tul_close(sock);
    } else
#endif
    {
        if (tul_send(sock, req.data(), (int)req.size(), 0) < 0) {
            tul_close(sock);
            out_err = "send failed";
            return false;
        }
        char chunk[4096];
        for (;;) {
            ssize_local_t n = tul_recv(sock, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            buf.append(chunk, n);
            if (buf.size() > 64 * 1024 * 1024) break;  // 64MB cap
        }
        tul_close(sock);
    }

    if (buf.empty()) {
        out_err = "empty response";
        return false;
    }

    size_t le = buf.find('\n');
    if (le == std::string::npos) {
        out_err = "malformed response";
        return false;
    }
    std::string status_line = buf.substr(0, le);
    if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();
    size_t sp = status_line.find(' ');
    if (sp == std::string::npos) {
        out_err = "malformed status line";
        return false;
    }
    size_t sp2 = status_line.find(' ', sp + 1);
    std::string code = status_line.substr(
        sp + 1, (sp2 == std::string::npos ? status_line.size() : sp2) - sp - 1);
    out_status = std::atoi(code.c_str());

    // Skip past `\r\n\r\n` to find the body.
    size_t body_start = buf.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        body_start = buf.find("\n\n");
        if (body_start != std::string::npos) body_start += 2;
        else body_start = buf.size();
    } else {
        body_start += 4;
    }
    if (body_start < buf.size()) {
        out_body = buf.substr(body_start);
    }
    return true;
}

bool http_request_url(const std::string &method, const std::string &url,
                      const std::string &body, std::string &out_full,
                      std::string &out_err,
                      const std::string &extra_headers) {
    out_full.clear();
    out_err.clear();

    std::string host, path;
    int port;
    bool is_https = false;
    if (!parse_http_url_internal(url, host, port, path, is_https)) {
        out_err = "bad url (use http:// or https://)";
        return false;
    }
#if !defined(TULPAR_HAS_TLS)
    if (is_https) {
        out_err = "TLS not compiled in (build Tulpar with OpenSSL to enable https://)";
        return false;
    }
#endif

#if defined(_WIN32)
    static bool g_wsa_started = false;
    if (!g_wsa_started) {
        WSADATA d; WSAStartup(MAKEWORD(2, 2), &d);
        g_wsa_started = true;
    }
#endif

    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), port_str, &hints, &res) != 0 || !res) {
        out_err = "dns failed for " + host;
        return false;
    }
    tul_socket sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == TUL_INVALID) {
        freeaddrinfo(res);
        out_err = "socket() failed";
        return false;
    }
    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) < 0) {
        tul_close(sock);
        freeaddrinfo(res);
        out_err = "connect failed";
        return false;
    }
    freeaddrinfo(res);

    std::string req;
    req.reserve(256 + body.size());
    req += method;
    req += ' ';
    req += path;
    req += " HTTP/1.0\r\nHost: ";
    req += host;
    if ((!is_https && port != 80) || (is_https && port != 443)) {
        char pb[16]; std::snprintf(pb, sizeof(pb), ":%d", port);
        req += pb;
    }
    req += "\r\nUser-Agent: tulpar/0.2\r\nAccept: */*\r\n";
    if (!body.empty()) {
        char clen[40];
        std::snprintf(clen, sizeof(clen), "Content-Length: %zu\r\n", body.size());
        req += clen;
        req += "Content-Type: application/json\r\n";
    }
    if (!extra_headers.empty()) {
        req += extra_headers;
    }
    req += "Connection: close\r\n\r\n";
    req += body;

#if defined(TULPAR_HAS_TLS)
    if (is_https) {
        SSL_CTX *ctx = make_client_tls_ctx(out_err);
        if (!ctx) { tul_close(sock); return false; }
        SSL *ssl = SSL_new(ctx);
        if (!ssl) { SSL_CTX_free(ctx); tul_close(sock); out_err = "SSL_new failed"; return false; }
        apply_tls_hostname_check(ssl, host);
        SSL_set_fd(ssl, (int)sock);
        if (SSL_connect(ssl) != 1) {
            unsigned long ssl_err = ERR_peek_last_error();
            int verify_rc = (int)SSL_get_verify_result(ssl);
            SSL_free(ssl); SSL_CTX_free(ctx); tul_close(sock);
            char detail[160];
            if (verify_rc != X509_V_OK) {
                std::snprintf(detail, sizeof(detail),
                              " (cert verify: %s)",
                              X509_verify_cert_error_string(verify_rc));
            } else if (ssl_err) {
                std::snprintf(detail, sizeof(detail), " (ssl: 0x%lx)", ssl_err);
            } else {
                detail[0] = '\0';
            }
            out_err = "TLS handshake failed for " + host + detail;
            return false;
        }
        if (SSL_write(ssl, req.data(), (int)req.size()) <= 0) {
            SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); tul_close(sock);
            out_err = "TLS send failed";
            return false;
        }
        char chunk[4096];
        for (;;) {
            int n = SSL_read(ssl, chunk, sizeof(chunk));
            if (n <= 0) break;
            out_full.append(chunk, n);
            if (out_full.size() > 64 * 1024 * 1024) break;
        }
        SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); tul_close(sock);
    } else
#endif
    {
        if (tul_send(sock, req.data(), (int)req.size(), 0) < 0) {
            tul_close(sock);
            out_err = "send failed";
            return false;
        }
        char chunk[4096];
        for (;;) {
            ssize_local_t n = tul_recv(sock, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            out_full.append(chunk, n);
            if (out_full.size() > 64 * 1024 * 1024) break;
        }
        tul_close(sock);
    }
    if (out_full.empty()) {
        out_err = "empty response";
        return false;
    }
    return true;
}

}  // namespace tulpar
