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
#else
    #define TULPAR_NORETURN __attribute__((noreturn))
    #define TULPAR_INLINE inline __attribute__((always_inline))
#endif

// Suppress warnings for specific compilers
#if COMPILER_MSVC
    // Disable specific MSVC warnings
    #pragma warning(disable: 4996) // 'function': was declared deprecated
#endif

#endif // TULPAR_PLATFORM_H
