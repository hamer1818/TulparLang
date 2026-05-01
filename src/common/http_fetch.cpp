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
        // Lazily initialise the OpenSSL library state. Idempotent
        // since OpenSSL 1.1; safe to call from multiple threads.
        static bool g_ssl_inited = false;
        if (!g_ssl_inited) {
            SSL_library_init();
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
            g_ssl_inited = true;
        }
        SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            tul_close(sock);
            out_err = "SSL_CTX_new failed";
            return false;
        }
        // Use the system trust store if discoverable; fall back to
        // disabling verification rather than refusing the request,
        // because most MSYS2 installs don't ship a usable trust store.
        // For production this needs a configurable cert path.
        SSL_CTX_set_default_verify_paths(ctx);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

        SSL *ssl = SSL_new(ctx);
        if (!ssl) {
            SSL_CTX_free(ctx);
            tul_close(sock);
            out_err = "SSL_new failed";
            return false;
        }
        SSL_set_tlsext_host_name(ssl, host.c_str());  // SNI
        SSL_set_fd(ssl, (int)sock);
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            tul_close(sock);
            out_err = "TLS handshake failed for " + host;
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
                      std::string &out_err) {
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
    req += "Connection: close\r\n\r\n";
    req += body;

#if defined(TULPAR_HAS_TLS)
    if (is_https) {
        static bool g_ssl_inited2 = false;
        if (!g_ssl_inited2) {
            SSL_library_init();
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
            g_ssl_inited2 = true;
        }
        SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) { tul_close(sock); out_err = "SSL_CTX_new failed"; return false; }
        SSL_CTX_set_default_verify_paths(ctx);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
        SSL *ssl = SSL_new(ctx);
        if (!ssl) { SSL_CTX_free(ctx); tul_close(sock); out_err = "SSL_new failed"; return false; }
        SSL_set_tlsext_host_name(ssl, host.c_str());
        SSL_set_fd(ssl, (int)sock);
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl); SSL_CTX_free(ctx); tul_close(sock);
            out_err = "TLS handshake failed for " + host;
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
