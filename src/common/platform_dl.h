#ifndef TULPAR_PLATFORM_DL_H
#define TULPAR_PLATFORM_DL_H

#include "platform.h"
#include <stdio.h>

// ============================================================================
// Cross-Platform Dynamic Library Loading
// ============================================================================

#if PLATFORM_WINDOWS
    #include <windows.h>
    
    // Library handle type
    typedef HMODULE tulpar_dl_handle_t;
    
    #ifdef __cplusplus
    extern "C" {
    #endif
    
    // Open dynamic library
    // On Windows: NULL handle means search in current process
    static inline tulpar_dl_handle_t tulpar_dlopen(const char *filename) {
        if (filename == NULL) {
            // RTLD_DEFAULT equivalent: return handle to current process
            return GetModuleHandle(NULL);
        }
        return LoadLibraryA(filename);
    }
    
    // Get symbol from library
    static inline void* tulpar_dlsym(tulpar_dl_handle_t handle, const char *symbol) {
        if (handle == NULL) {
            handle = GetModuleHandle(NULL);
        }
        return (void*)GetProcAddress(handle, symbol);
    }
    
    // Close dynamic library
    static inline int tulpar_dlclose(tulpar_dl_handle_t handle) {
        if (handle == NULL || handle == GetModuleHandle(NULL)) {
            // Don't close the main executable handle
            return 0;
        }
        return FreeLibrary(handle) ? 0 : -1;
    }
    
    // Get error message
    static inline const char* tulpar_dlerror(void) {
        static char error_buffer[256];
        DWORD error = GetLastError();
        
        if (error == 0) {
            return NULL;
        }
        
        DWORD result = FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            error_buffer,
            sizeof(error_buffer),
            NULL
        );
        
        if (result == 0) {
            snprintf(error_buffer, sizeof(error_buffer), "Error code: %lu", error);
        }
        
        // Remove trailing newline
        size_t len = strlen(error_buffer);
        if (len > 0 && error_buffer[len - 1] == '\n') {
            error_buffer[len - 1] = '\0';
            if (len > 1 && error_buffer[len - 2] == '\r') {
                error_buffer[len - 2] = '\0';
            }
        }
        
        return error_buffer;
    }
    
    // Special handle for searching current process (RTLD_DEFAULT equivalent)
    #define TULPAR_RTLD_DEFAULT ((tulpar_dl_handle_t)0)
    
    #ifdef __cplusplus
    }
    #endif
    
#else
    // UNIX/POSIX dynamic loading
    #include <dlfcn.h>
    
    // Library handle type
    typedef void* tulpar_dl_handle_t;
    
    #ifdef __cplusplus
    extern "C" {
    #endif
    
    // Open dynamic library
    static inline tulpar_dl_handle_t tulpar_dlopen(const char *filename) {
        if (filename == NULL) {
            return RTLD_DEFAULT;
        }
        return dlopen(filename, RTLD_LAZY | RTLD_LOCAL);
    }
    
    // Get symbol from library
    static inline void* tulpar_dlsym(tulpar_dl_handle_t handle, const char *symbol) {
        if (handle == NULL) {
            handle = RTLD_DEFAULT;
        }
        return dlsym(handle, symbol);
    }
    
    // Close dynamic library
    static inline int tulpar_dlclose(tulpar_dl_handle_t handle) {
        if (handle == NULL || handle == RTLD_DEFAULT) {
            return 0;
        }
        return dlclose(handle);
    }
    
    // Get error message
    static inline const char* tulpar_dlerror(void) {
        return dlerror();
    }
    
    // Special handle for searching current process
    #define TULPAR_RTLD_DEFAULT RTLD_DEFAULT
    
    #ifdef __cplusplus
    }
    #endif
    
#endif

// ============================================================================
// Higher-level Utilities
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Try to load a function from the current process or a library
static inline void* tulpar_dlsym_auto(const char *symbol_name) {
    void *symbol = tulpar_dlsym(TULPAR_RTLD_DEFAULT, symbol_name);
    if (!symbol) {
        const char *error = tulpar_dlerror();
        if (error) {
            fprintf(stderr, "Failed to load symbol '%s': %s\n", symbol_name, error);
        }
    }
    return symbol;
}

#ifdef __cplusplus
}
#endif

#endif // TULPAR_PLATFORM_DL_H
