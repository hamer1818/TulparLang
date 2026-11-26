#ifndef TULPAR_PLATFORM_SOCKETS_H
#define TULPAR_PLATFORM_SOCKETS_H

#include "platform.h"

// ============================================================================
// Cross-Platform Socket API
// ============================================================================

#if PLATFORM_WINDOWS
    // Windows Winsock2
    #include <winsock2.h>
    #include <ws2tcpip.h>
    
    // Type definitions
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    
    // Winsock initialization
    #ifdef __cplusplus
    extern "C" {
    #endif
    
    static inline int tulpar_socket_init(void) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        return (result == 0) ? 0 : -1;
    }
    
    static inline void tulpar_socket_cleanup(void) {
        WSACleanup();
    }
    
    static inline int tulpar_socket_close(socket_t s) {
        return closesocket(s);
    }
    
    static inline int tulpar_socket_get_error(void) {
        return WSAGetLastError();
    }
    
    static inline void tulpar_socket_set_error(int err) {
        WSASetLastError(err);
    }
    
    #ifdef __cplusplus
    }
    #endif
    
    // Winsock doesn't have these POSIX error codes, map to Windows equivalents
    #ifndef EWOULDBLOCK
        #define EWOULDBLOCK WSAEWOULDBLOCK
    #endif
    #ifndef EINPROGRESS
        #define EINPROGRESS WSAEINPROGRESS
    #endif
    #ifndef ECONNRESET
        #define ECONNRESET WSAECONNRESET
    #endif
    
#else
    // UNIX/POSIX sockets
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <unistd.h>
    
    // Type definitions
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE (-1)
    #define SOCKET_ERROR_VALUE (-1)
    
    #ifdef __cplusplus
    extern "C" {
    #endif
    
    static inline int tulpar_socket_init(void) {
        // No initialization needed on UNIX
        return 0;
    }
    
    static inline void tulpar_socket_cleanup(void) {
        // No cleanup needed on UNIX
    }
    
    static inline int tulpar_socket_close(socket_t s) {
        return close(s);
    }
    
    static inline int tulpar_socket_get_error(void) {
        return errno;
    }
    
    static inline void tulpar_socket_set_error(int err) {
        errno = err;
    }
    
    #ifdef __cplusplus
    }
    #endif
    
#endif

// SIGPIPE: kapanmis bir sokete (karsi taraf RST etti, SMTP sunucusu
// baglantiyi kesti) ikinci kez yazmak POSIX'te sureci SIGPIPE ile OLDURUR —
// Wings sunucusu dahil. Olculdu (2026-11-25, smtp_smoke sabotaji): istemci
// cikis kodu 141. Her soket/TLS acilisindan once cagrilir ve SIGPIPE'i yok
// sayar; yazma -1/EPIPE doner, kod onu hata olarak gorur (Rust, Python ve
// Go'nun soketler icin yaptigi). Yalniz AG KULLANAN programda kurulur:
// `./prog | head` gibi ag kullanmayan bir programda stdout'un kapanmasi
// eskisi gibi sureci bitirir. Kapisi: tests/sigpipe_kapisi.sh.
#if !defined(_WIN32)
    #include <signal.h>
    static inline void tulpar_net_ignore_sigpipe(void) {
        static volatile int done = 0;  // yaris zararsiz: signal() es-kuvvetli
        if (!done) {
            done = 1;
            signal(SIGPIPE, SIG_IGN);
        }
    }
#else
    static inline void tulpar_net_ignore_sigpipe(void) {}
#endif

// ============================================================================
// Common Socket Functions (Platform-Independent Interface)
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Set socket to non-blocking mode
static inline int tulpar_socket_set_nonblocking(socket_t sock, int nonblocking) {
#if PLATFORM_WINDOWS
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return -1;
    
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    return fcntl(sock, F_SETFL, flags);
#endif
}

// Check if socket error is "would block"
static inline int tulpar_socket_would_block(int error_code) {
#if PLATFORM_WINDOWS
    return (error_code == WSAEWOULDBLOCK);
#else
    return (error_code == EWOULDBLOCK || error_code == EAGAIN);
#endif
}

// BLOKLAMAYAN gonderim (2026-11-26, listen_evented yazma yolu): soket bloklu
// olsa da bu cagri beklemez. Donus: gonderilen bayt (>= 0; 0 = tampon dolu,
// sonra dene) ya da -1 (baglanti hatasi). POSIX: MSG_DONTWAIT (+ Linux'ta
// MSG_NOSIGNAL; SIGPIPE zaten tulpar_net_ignore_sigpipe ile kapali). Windows'ta
// MSG_DONTWAIT yok: cagri suresince FIONBIO acilip kapatilir.
static inline long tulpar_socket_send_nb(socket_t fd, const char *buf, size_t len) {
#if PLATFORM_WINDOWS
    u_long on = 1, off = 0;
    ioctlsocket(fd, FIONBIO, &on);
    int n = send(fd, buf, (int)(len > 0x7fffffff ? 0x7fffffff : len), 0);
    int err = (n < 0) ? WSAGetLastError() : 0;
    ioctlsocket(fd, FIONBIO, &off);
    if (n >= 0) return n;
    return err == WSAEWOULDBLOCK ? 0 : -1;
#else
    int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    for (;;) {
        ssize_t n = send(fd, buf, len, flags);
        if (n >= 0) return (long)n;
        if (errno == EINTR) continue;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
#endif
}

#ifdef __cplusplus
}
#endif

// ============================================================================
// poll() / WSAPoll() — same API on POSIX and Windows
// ============================================================================
//
// Used by `listen_evented` in lib/wings.tpr to multiplex many client
// sockets on a single thread without spawning threads per connection.
// We expose the POSIX `struct pollfd` shape directly because Winsock2's
// WSAPOLLFD has identical layout (validated on MSYS2 mingw-w64);
// callers fill in `events` (POLLIN / POLLOUT) and read `revents` after
// the call.
//
// Return: count of fds with non-zero `revents`, or -1 on error.

#if PLATFORM_WINDOWS
    // WSAPOLLFD lives in <winsock2.h> already included above. WSAPoll
    // ships in Vista+ — every Windows we target.
    typedef WSAPOLLFD tulpar_pollfd;
#else
    #include <poll.h>
    #include <sys/time.h>
    typedef struct pollfd tulpar_pollfd;
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline int tulpar_socket_poll(tulpar_pollfd *fds, unsigned int nfds,
                                     int timeout_ms) {
#if PLATFORM_WINDOWS
    return WSAPoll(fds, (ULONG)nfds, timeout_ms);
#else
    return poll(fds, (nfds_t)nfds, timeout_ms);
#endif
}

// Wait until `fd` is readable (or hung up / errored) for at most
// `timeout_ms`. Returns 1 = readable (a recv will not block), 0 = timed
// out, -1 = poll error. EINTR is retried with the full timeout again —
// callers that enforce a deadline recompute the remainder themselves.
static inline int tulpar_socket_wait_readable(socket_t fd, int timeout_ms) {
    for (;;) {
        tulpar_pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        int rc = tulpar_socket_poll(&p, 1, timeout_ms);
        if (rc > 0) return 1;
        if (rc == 0) return 0;
#if !PLATFORM_WINDOWS
        if (errno == EINTR) continue;
#endif
        return -1;
    }
}

// The socket's SO_RCVTIMEO in milliseconds (0 = none / unknown). Lets a
// poll()-based reader keep honouring a per-recv timeout that was set with
// `socket_timeout(fd, ms)` — once a poll says "readable", recv itself no
// longer waits, so the cap has to be applied to the poll instead.
static inline int tulpar_socket_get_rcvtimeo_ms(socket_t fd) {
#if PLATFORM_WINDOWS
    DWORD t = 0;
    int len = (int)sizeof(t);
    if (getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t, &len) != 0) return 0;
    return t > 0x7fffffffUL ? 0x7fffffff : (int)t;
#else
    struct timeval tv;
    socklen_t len = (socklen_t)sizeof(tv);
    if (getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, &len) != 0) return 0;
    long long ms = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    if (ms <= 0 && (tv.tv_sec > 0 || tv.tv_usec > 0)) ms = 1;
    return ms > 0x7fffffffLL ? 0x7fffffff : (int)ms;
#endif
}

#ifdef __cplusplus
}
#endif

#endif // TULPAR_PLATFORM_SOCKETS_H
