#ifndef TULPAR_PLATFORM_THREADS_H
#define TULPAR_PLATFORM_THREADS_H

#include "platform.h"

// ============================================================================
// Cross-Platform Threading API
// ============================================================================
// Note: Modern approach uses C++11 std::thread, but we provide low-level
// platform-specific wrappers for compatibility with C code and fine control.
// ============================================================================

#if PLATFORM_WINDOWS
    #include <windows.h>
    #include <process.h>
    
    // Thread types
    typedef HANDLE tulpar_thread_t;
    typedef CRITICAL_SECTION tulpar_mutex_t;
    typedef DWORD tulpar_thread_id_t;
    typedef unsigned (__stdcall *tulpar_thread_func_t)(void*);
    
    #ifdef __cplusplus
    extern "C" {
    #endif
    
    // Thread creation
    static inline int tulpar_thread_create(tulpar_thread_t *thread, tulpar_thread_func_t func, void *arg) {
        *thread = (HANDLE)_beginthreadex(NULL, 0, func, arg, 0, NULL);
        return (*thread != NULL) ? 0 : -1;
    }
    
    // Thread join
    static inline int tulpar_thread_join(tulpar_thread_t thread) {
        DWORD result = WaitForSingleObject(thread, INFINITE);
        if (result == WAIT_OBJECT_0) {
            CloseHandle(thread);
            return 0;
        }
        return -1;
    }
    
    // Thread detach
    static inline int tulpar_thread_detach(tulpar_thread_t thread) {
        return CloseHandle(thread) ? 0 : -1;
    }
    
    // Get current thread ID
    static inline tulpar_thread_id_t tulpar_thread_self(void) {
        return GetCurrentThreadId();
    }
    
    // Thread sleep (milliseconds)
    static inline void tulpar_thread_sleep(unsigned int milliseconds) {
        Sleep(milliseconds);
    }
    
    // Mutex operations
    static inline int tulpar_mutex_init(tulpar_mutex_t *mutex) {
        InitializeCriticalSection(mutex);
        return 0;
    }
    
    static inline int tulpar_mutex_destroy(tulpar_mutex_t *mutex) {
        DeleteCriticalSection(mutex);
        return 0;
    }
    
    static inline int tulpar_mutex_lock(tulpar_mutex_t *mutex) {
        EnterCriticalSection(mutex);
        return 0;
    }
    
    static inline int tulpar_mutex_unlock(tulpar_mutex_t *mutex) {
        LeaveCriticalSection(mutex);
        return 0;
    }
    
    static inline int tulpar_mutex_trylock(tulpar_mutex_t *mutex) {
        return TryEnterCriticalSection(mutex) ? 0 : -1;
    }
    
    #ifdef __cplusplus
    }
    #endif
    
#else
    // POSIX threads (pthread)
    #include <pthread.h>
    #include <unistd.h>
    
    // Thread types
    typedef pthread_t tulpar_thread_t;
    typedef pthread_mutex_t tulpar_mutex_t;
    typedef pthread_t tulpar_thread_id_t;
    typedef void* (*tulpar_thread_func_t)(void*);
    
    #ifdef __cplusplus
    extern "C" {
    #endif
    
    // Thread creation wrapper for POSIX
    static inline int tulpar_thread_create(tulpar_thread_t *thread, tulpar_thread_func_t func, void *arg) {
        return pthread_create(thread, NULL, func, arg);
    }
    
    // Thread join
    static inline int tulpar_thread_join(tulpar_thread_t thread) {
        return pthread_join(thread, NULL);
    }
    
    // Thread detach
    static inline int tulpar_thread_detach(tulpar_thread_t thread) {
        return pthread_detach(thread);
    }
    
    // Get current thread ID
    static inline tulpar_thread_id_t tulpar_thread_self(void) {
        return pthread_self();
    }
    
    // Thread sleep (milliseconds)
    static inline void tulpar_thread_sleep(unsigned int milliseconds) {
        usleep(milliseconds * 1000);
    }
    
    // Mutex operations
    static inline int tulpar_mutex_init(tulpar_mutex_t *mutex) {
        return pthread_mutex_init(mutex, NULL);
    }
    
    static inline int tulpar_mutex_destroy(tulpar_mutex_t *mutex) {
        return pthread_mutex_destroy(mutex);
    }
    
    static inline int tulpar_mutex_lock(tulpar_mutex_t *mutex) {
        return pthread_mutex_lock(mutex);
    }
    
    static inline int tulpar_mutex_unlock(tulpar_mutex_t *mutex) {
        return pthread_mutex_unlock(mutex);
    }
    
    static inline int tulpar_mutex_trylock(tulpar_mutex_t *mutex) {
        return pthread_mutex_trylock(mutex);
    }
    
    #ifdef __cplusplus
    }
    #endif
    
#endif

// ============================================================================
// High-level Thread Utilities
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Get number of CPU cores
static inline int tulpar_get_cpu_count(void) {
#if PLATFORM_WINDOWS
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (int)sysinfo.dwNumberOfProcessors;
#else
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

#ifdef __cplusplus
}
#endif

#endif // TULPAR_PLATFORM_THREADS_H
