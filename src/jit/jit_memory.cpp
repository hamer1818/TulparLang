// ============================================================================
// TULPAR JIT - Executable Memory Management
// ============================================================================

#include "jit.hpp"
#include "../common/localization.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>

// ============================================================================
// PLATFORM-SPECIFIC MEMORY ALLOCATION
// ============================================================================

// Unix/Linux/macOS: Use mmap for executable memory
static void *alloc_executable_memory(size_t size) {
    void *ptr = mmap(nullptr, size,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);
    return ptr == MAP_FAILED ? nullptr : ptr;
}

static void free_executable_memory(void *ptr, size_t size) {
    munmap(ptr, size);
}

static int make_memory_executable(void *ptr, size_t size) {
    return mprotect(ptr, size, PROT_READ | PROT_EXEC) == 0;
}

// ============================================================================
// JIT CODE BUFFER IMPLEMENTATION
// ============================================================================

JITCode *jit_code_alloc(size_t initial_size) {
    JITCode *code = static_cast<JITCode*>(malloc(sizeof(JITCode)));
    if (!code) return nullptr;
    
    code->code = (uint8_t *)alloc_executable_memory(initial_size);
    if (!code->code) {
        free(code);
        return nullptr;
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
    fprintf(stderr, tulpar::i18n::tr_for_en("JIT Error: Code buffer overflow (need %zu, have %zu)\n"),
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
