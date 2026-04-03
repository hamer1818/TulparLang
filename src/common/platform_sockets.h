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

#ifdef __cplusplus
}
#endif

#endif // TULPAR_PLATFORM_SOCKETS_H
