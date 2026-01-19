// ============================================================================
// TULPAR JIT - Executable Memory Management
// ============================================================================

#include "jit.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// ============================================================================
// PLATFORM-SPECIFIC MEMORY ALLOCATION
// ============================================================================

#ifdef _WIN32

// Windows: Use VirtualAlloc for executable memory
static void *alloc_executable_memory(size_t size) {
    return VirtualAlloc(NULL, size, 
                        MEM_COMMIT | MEM_RESERVE, 
                        PAGE_READWRITE);  // Start as RW, make exec later
}

static void free_executable_memory(void *ptr, size_t size) {
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
}

static int make_memory_executable(void *ptr, size_t size) {
    DWORD old_protect;
    return VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &old_protect) != 0;
}

#else

// Unix/Linux/macOS: Use mmap for executable memory
static void *alloc_executable_memory(size_t size) {
    void *ptr = mmap(NULL, size,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);
    return ptr == MAP_FAILED ? NULL : ptr;
}

static void free_executable_memory(void *ptr, size_t size) {
    munmap(ptr, size);
}

static int make_memory_executable(void *ptr, size_t size) {
    return mprotect(ptr, size, PROT_READ | PROT_EXEC) == 0;
}

#endif

// ============================================================================
// JIT CODE BUFFER IMPLEMENTATION
// ============================================================================

JITCode *jit_code_alloc(size_t initial_size) {
    JITCode *code = (JITCode *)malloc(sizeof(JITCode));
    if (!code) return NULL;
    
    code->code = (uint8_t *)alloc_executable_memory(initial_size);
    if (!code->code) {
        free(code);
        return NULL;
    }
    
    code->size = 0;
    code->capacity = initial_size;
    code->is_executable = 0;
    
    return code;
}

void jit_code_free(JITCode *code) {
    if (!code) return;
    
    if (code->code) {
        free_executable_memory(code->code, code->capacity);
    }
    free(code);
}

int jit_code_make_executable(JITCode *code) {
    if (!code || code->is_executable) return code ? 1 : 0;
    
    if (make_memory_executable(code->code, code->capacity)) {
        code->is_executable = 1;
        return 1;
    }
    return 0;
}

// Ensure buffer has enough space
static int jit_ensure_capacity(JITCode *code, size_t needed) {
    if (code->size + needed <= code->capacity) {
        return 1;
    }
    
    // Can't realloc executable memory easily, so we fail
    // In production, we'd allocate a new larger buffer and copy
    fprintf(stderr, "JIT Error: Code buffer overflow (need %zu, have %zu)\n",
            code->size + needed, code->capacity);
    return 0;
}

void jit_emit_byte(JITCode *code, uint8_t byte) {
    if (!jit_ensure_capacity(code, 1)) return;
    code->code[code->size++] = byte;
}

void jit_emit_bytes(JITCode *code, const uint8_t *bytes, size_t count) {
    if (!jit_ensure_capacity(code, count)) return;
    memcpy(code->code + code->size, bytes, count);
    code->size += count;
}

size_t jit_code_pos(JITCode *code) {
    return code->size;
}

// ============================================================================
// JIT COMPILED FUNCTION MANAGEMENT
// ============================================================================

void jit_free_compiled(JITCompiledFunc *compiled) {
    if (!compiled) return;
    
    if (compiled->code) {
        jit_code_free(compiled->code);
    }
    free(compiled);
}
