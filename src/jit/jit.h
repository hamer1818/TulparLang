#ifndef JIT_H
#define JIT_H

// ============================================================================
// TULPAR JIT COMPILER - Just-In-Time Native Code Generation
// ============================================================================

#include <stddef.h>
#include <stdint.h>


// Forward declarations (avoid circular include with vm.h)
typedef struct VM VM;
typedef struct ObjFunction ObjFunction;

// ============================================================================
// JIT CONFIGURATION
// ============================================================================

#define JIT_THRESHOLD                                                          \
  10 // Compile after N calls (lowered from 100 for better hot function
     // detection)
#define JIT_CODE_SIZE_INITIAL 4096 // Initial code buffer size

// JIT only works on x86_64 architecture
// Automatically disable on ARM64 (Apple Silicon, Raspberry Pi, etc.)
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
#define JIT_ENABLED 1
#else
#define JIT_ENABLED 0
#warning "JIT disabled: ARM64/non-x86_64 architecture detected"
#endif

// ============================================================================
// JIT CODE BUFFER
// ============================================================================

typedef struct JITCode {
  uint8_t *code;     // Executable code buffer
  size_t size;       // Current code size
  size_t capacity;   // Buffer capacity
  int is_executable; // Has been made executable
} JITCode;

// ============================================================================
// JIT COMPILED FUNCTION
// ============================================================================

typedef void (*JITFunction)(VM *vm);

typedef struct JITCompiledFunc {
  JITFunction entry; // Entry point
  JITCode *code;     // Code buffer (for cleanup)
  int valid;         // Is this still valid (not deoptimized)
} JITCompiledFunc;

// ============================================================================
// JIT MEMORY FUNCTIONS
// ============================================================================

// Allocate JIT code buffer
JITCode *jit_code_alloc(size_t initial_size);

// Free JIT code buffer
void jit_code_free(JITCode *code);

// Make code executable (call after writing all code)
int jit_code_make_executable(JITCode *code);

// Write byte to code buffer
void jit_emit_byte(JITCode *code, uint8_t byte);

// Write multiple bytes
void jit_emit_bytes(JITCode *code, const uint8_t *bytes, size_t count);

// Get current code position
size_t jit_code_pos(JITCode *code);

// ============================================================================
// JIT COMPILER FUNCTIONS
// ============================================================================

// Compile a function to native code
JITCompiledFunc *jit_compile_function(VM *vm, ObjFunction *func);

// Free compiled function
void jit_free_compiled(JITCompiledFunc *compiled);

// Check if function should be JIT compiled
int jit_should_compile(ObjFunction *func);

// ============================================================================
// JIT PROFILER
// ============================================================================

// Increment call count and check for JIT threshold
int jit_profile_call(ObjFunction *func);

#endif // JIT_H
