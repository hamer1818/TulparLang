#ifndef TULPAR_PLATFORM_H
#define TULPAR_PLATFORM_H

// ============================================================================
// Platform Detection and Common Definitions
// ============================================================================

// Platform detection macros
#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || defined(__WINDOWS__)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_LINUX 0
    #define PLATFORM_MACOS 0
    #define PLATFORM_UNIX 0
#elif defined(__APPLE__) && defined(__MACH__)
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_LINUX 0
    #define PLATFORM_MACOS 1
    #define PLATFORM_UNIX 1
#elif defined(__linux__)
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_LINUX 1
    #define PLATFORM_MACOS 0
    #define PLATFORM_UNIX 1
#else
    #define PLATFORM_WINDOWS 0
    #define PLATFORM_LINUX 0
    #define PLATFORM_MACOS 0
    #define PLATFORM_UNIX 1
#endif

// Compiler detection
#if defined(_MSC_VER)
    #define COMPILER_MSVC 1
    #define COMPILER_GCC 0
    #define COMPILER_CLANG 0
#elif defined(__clang__)
    #define COMPILER_MSVC 0
    #define COMPILER_GCC 0
    #define COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define COMPILER_MSVC 0
    #define COMPILER_GCC 1
    #define COMPILER_CLANG 0
#else
    #define COMPILER_MSVC 0
    #define COMPILER_GCC 0
    #define COMPILER_CLANG 0
#endif

// Common includes
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Platform-specific includes
#if PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/time.h>
#endif

// Path separator
#if PLATFORM_WINDOWS
    #define PATH_SEPARATOR '\\'
    #define PATH_SEPARATOR_STR "\\"
#else
    #define PATH_SEPARATOR '/'
    #define PATH_SEPARATOR_STR "/"
#endif

// Export/Import macros for shared libraries
#if PLATFORM_WINDOWS
    #if COMPILER_MSVC
        #define TULPAR_EXPORT __declspec(dllexport)
        #define TULPAR_IMPORT __declspec(dllimport)
    #else
        #define TULPAR_EXPORT __attribute__((dllexport))
        #define TULPAR_IMPORT __attribute__((dllimport))
    #endif
#else
    #define TULPAR_EXPORT __attribute__((visibility("default")))
    #define TULPAR_IMPORT
#endif

// Function attributes
#if COMPILER_MSVC
    #define TULPAR_NORETURN __declspec(noreturn)
    #define TULPAR_INLINE __forceinline
    #define TULPAR_NOINLINE __declspec(noinline)
#else
    #define TULPAR_NORETURN __attribute__((noreturn))
    #define TULPAR_INLINE inline __attribute__((always_inline))
    #define TULPAR_NOINLINE __attribute__((noinline))
#endif

// Thread-local storage
#if COMPILER_MSVC
    #define TULPAR_THREAD_LOCAL __declspec(thread)
#else
    #define TULPAR_THREAD_LOCAL __thread
#endif

// Suppress warnings for specific compilers
#if COMPILER_MSVC
    // Disable specific MSVC warnings
    #pragma warning(disable: 4996) // 'function': was declared deprecated
#endif

// ============================================================================
// Cross-Platform Time Functions
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Get current time in milliseconds (monotonic clock for measurements)
static inline double tulpar_clock_ms(void) {
#if PLATFORM_WINDOWS
    static LARGE_INTEGER frequency = {0};
    static LARGE_INTEGER start_time = {0};
    LARGE_INTEGER current_time;
    
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start_time);
    }
    
    QueryPerformanceCounter(&current_time);
    return (double)(current_time.QuadPart - start_time.QuadPart) * 1000.0 / (double)frequency.QuadPart;
#else
    struct timeval tv;
    static double start_time = 0;
    gettimeofday(&tv, NULL);
    double ms = tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
    if (start_time == 0) {
        start_time = ms;
    }
    return ms - start_time;
#endif
}

// Sleep for specified milliseconds
static inline void tulpar_sleep_ms(unsigned int milliseconds) {
#if PLATFORM_WINDOWS
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

// Get Unix timestamp in seconds
static inline int64_t tulpar_time_unix(void) {
#if PLATFORM_WINDOWS
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // Convert from 100-nanosecond intervals since Jan 1, 1601 to Unix epoch
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec;
#endif
}

#ifdef __cplusplus
}
#endif

#endif // TULPAR_PLATFORM_H
