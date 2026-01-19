// ============================================================================
// TULPAR JIT - Optimization Passes
// ============================================================================

#include "jit.h"
#include "x64_emit.h"
#include "../vm/vm.h"
#include "../vm/bytecode.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// CONSTANT FOLDING
// ============================================================================

// Check if bytecode sequence can be constant-folded
// Returns folded value and number of bytecodes consumed, or 0 if no folding possible
int jit_try_constant_fold(uint8_t *ip, Chunk *chunk, int64_t *result) {
    // Pattern: CONST_INT a, CONST_INT b, OP (ADD/SUB/MUL)
    if (ip[0] != OP_CONST_INT) return 0;
    
    uint16_t idx1 = ip[1] | (ip[2] << 8);
    if (chunk->constants[idx1].type != CONST_INT) return 0;
    int64_t a = chunk->constants[idx1].int_val;
    
    uint8_t *next = ip + 3;
    if (next[0] != OP_CONST_INT) return 0;
    
    uint16_t idx2 = next[1] | (next[2] << 8);
    if (chunk->constants[idx2].type != CONST_INT) return 0;
    int64_t b = chunk->constants[idx2].int_val;
    
    uint8_t *op = next + 3;
    switch (*op) {
        case OP_ADD:
            *result = a + b;
            return 7;  // 3 + 3 + 1 bytes consumed
        case OP_SUB:
            *result = a - b;
            return 7;
        case OP_MUL:
            *result = a * b;
            return 7;
        default:
            return 0;
    }
}

// ============================================================================
// STRENGTH REDUCTION
// ============================================================================

// Check for multiply by power of 2 -> shift
int jit_is_power_of_2(int64_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

int jit_log2(int64_t n) {
    int log = 0;
    while (n > 1) {
        n >>= 1;
        log++;
    }
    return log;
}

// ============================================================================
// PEEPHOLE OPTIMIZATION
// ============================================================================

// Pattern: LOAD_LOCAL x, STORE_LOCAL x -> NOP (redundant load/store)
int jit_is_redundant_load_store(uint8_t *ip) {
    if (ip[0] != OP_LOAD_LOCAL) return 0;
    uint16_t slot1 = ip[1] | (ip[2] << 8);
    
    if (ip[3] != OP_STORE_LOCAL) return 0;
    uint16_t slot2 = ip[4] | (ip[5] << 8);
    
    return slot1 == slot2 ? 6 : 0;
}

// Pattern: PUSH, POP -> NOP
int jit_is_push_pop(uint8_t *ip) {
    // Various push operations followed by POP
    int push_size = 0;
    
    switch (ip[0]) {
        case OP_CONST_INT:
        case OP_CONST_FLOAT:
        case OP_CONST_STR:
            push_size = 3;
            break;
        case OP_CONST_TRUE:
        case OP_CONST_FALSE:
        case OP_CONST_VOID:
            push_size = 1;
            break;
        case OP_LOAD_LOCAL:
            push_size = 3;
            break;
        default:
            return 0;
    }
    
    if (ip[push_size] == OP_POP) {
        return push_size + 1;
    }
    return 0;
}

// ============================================================================
// LOOP OPTIMIZATION
// ============================================================================

// Detect loop pattern and optimize
typedef struct {
    int start_offset;
    int end_offset;
    int back_edge_offset;
    int iteration_count;  // -1 if unknown
} LoopInfo;

// Find loop in bytecode
int jit_find_loop(uint8_t *code, int length, int offset, LoopInfo *info) {
    // Look for OP_LOOP instruction
    for (int i = offset; i < length; i++) {
        if (code[i] == OP_LOOP) {
            uint16_t back_offset = code[i+1] | (code[i+2] << 8);
            info->end_offset = i;
            info->start_offset = i + 3 - back_offset;
            info->back_edge_offset = i;
            info->iteration_count = -1;  // Unknown
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// DEAD CODE ELIMINATION
// ============================================================================

// Mark unreachable code after unconditional jumps
void jit_mark_dead_code(uint8_t *code, int length, uint8_t *reachable) {
    memset(reachable, 1, length);  // Assume all reachable
    
    for (int i = 0; i < length; ) {
        uint8_t op = code[i];
        
        switch (op) {
            case OP_JUMP: {
                // Mark bytes between jump and target as potentially dead
                // (simplified - full analysis needs control flow graph)
                i += 3;
                break;
            }
            case OP_RETURN:
            case OP_RETURN_VOID:
            case OP_HALT:
                // Everything after return until next label is dead
                i++;
                while (i < length && reachable[i]) {
                    // Check if this is a jump target
                    // Simplified: just mark next instruction as potentially dead
                    reachable[i] = 0;
                    i++;
                    break;  // Only mark one instruction
                }
                break;
            default:
                i++;
                break;
        }
    }
}

// ============================================================================
// FUNCTION INLINING (Faz 5)
// ============================================================================

// Check if function is a leaf (no calls)
int jit_is_leaf_function(ObjFunction *func) {
    if (!func || !func->chunk.code) return 0;
    
    uint8_t *ip = func->chunk.code;
    uint8_t *end = ip + func->chunk.code_length;
    
    while (ip < end) {
        uint8_t op = *ip;
        
        switch (op) {
            case OP_CALL:
            case OP_CALL_FAST:
            case OP_CALL_0:
            case OP_CALL_1:
            case OP_CALL_2:
            case OP_TAIL_CALL:
            case OP_CALL_BUILTIN:
                return 0;  // Has a call - not a leaf
                
            case OP_CONST_INT:
            case OP_CONST_FLOAT:
            case OP_CONST_STR:
            case OP_CONST_FUNC:
            case OP_LOAD_LOCAL:
            case OP_STORE_LOCAL:
            case OP_LOAD_GLOBAL:
            case OP_STORE_GLOBAL:
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
            case OP_JUMP_IF_TRUE:
            case OP_LOOP:
            case OP_AND:
            case OP_OR:
                ip += 3;
                break;
            default:
                ip++;
                break;
        }
    }
    
    return 1;  // No calls found - it's a leaf!
}

// Check if function is small enough to inline
int jit_should_inline(ObjFunction *func) {
    if (!func) return 0;
    
    // Only inline small leaf functions
    // Threshold: 20 bytecode ops
    if (func->chunk.code_length > 60) return 0;  // ~20 ops * 3 bytes avg
    
    return jit_is_leaf_function(func);
}

// ============================================================================
// ESCAPE ANALYSIS (Faz 5)
// ============================================================================

// Simple escape analysis: check if a value escapes current function
// Returns 1 if value definitely escapes, 0 if might not
int jit_value_escapes(uint8_t *code, int length, int local_slot) {
    // Simplified: check if local is ever passed to a call
    for (int i = 0; i < length; ) {
        uint8_t op = code[i];
        
        switch (op) {
            case OP_LOAD_LOCAL: {
                uint16_t slot = code[i+1] | (code[i+2] << 8);
                if (slot == local_slot) {
                    // Check what happens next
                    if (i + 3 < length) {
                        uint8_t next = code[i + 3];
                        if (next == OP_CALL || next == OP_CALL_FAST ||
                            next == OP_CALL_1 || next == OP_CALL_2 ||
                            next == OP_RETURN || next == OP_RETURN_FAST ||
                            next == OP_STORE_GLOBAL) {
                            return 1;  // Escapes!
                        }
                    }
                }
                i += 3;
                break;
            }
            case OP_CONST_INT:
            case OP_CONST_FLOAT:
            case OP_CONST_STR:
            case OP_CONST_FUNC:
            case OP_STORE_LOCAL:
            case OP_LOAD_GLOBAL:
            case OP_STORE_GLOBAL:
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
            case OP_JUMP_IF_TRUE:
            case OP_LOOP:
            case OP_AND:
            case OP_OR:
                i += 3;
                break;
            default:
                i++;
                break;
        }
    }
    
    return 0;  // Doesn't escape
}

// ============================================================================
// REGISTER ALLOCATION (Simple linear scan)
// ============================================================================

#define NUM_REGS 6
#define REG_NONE -1

typedef struct {
    int var_slot;       // Which local variable
    int start;          // First use
    int end;            // Last use
    int assigned_reg;   // Assigned register or REG_NONE
} LiveInterval;

// Simple allocator - assign registers to most frequently used locals
void jit_allocate_registers(Chunk *chunk, int *slot_to_reg, int max_slots) {
    // Initialize all slots to no register
    for (int i = 0; i < max_slots; i++) {
        slot_to_reg[i] = REG_NONE;
    }
    
    // Count uses of each local
    int *use_count = (int *)calloc(max_slots, sizeof(int));
    
    uint8_t *ip = chunk->code;
    uint8_t *end = ip + chunk->code_length;
    
    while (ip < end) {
        switch (*ip) {
            case OP_LOAD_LOCAL:
            case OP_STORE_LOCAL: {
                uint16_t slot = ip[1] | (ip[2] << 8);
                if (slot < (uint16_t)max_slots) {
                    use_count[slot]++;
                }
                ip += 3;
                break;
            }
            case OP_CONST_INT:
            case OP_CONST_FLOAT:
            case OP_CONST_STR:
            case OP_CONST_FUNC:
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
            case OP_JUMP_IF_TRUE:
            case OP_LOOP:
            case OP_AND:
            case OP_OR:
                ip += 3;
                break;
            case OP_CALL:
            case OP_TAIL_CALL:
            case OP_CALL_BUILTIN:
            case OP_PRINT:
                ip += 2;
                break;
            default:
                ip++;
                break;
        }
    }
    
    // Assign registers to top NUM_REGS most used locals
    // Available: R14, R15 (callee-saved, not used by JIT)
    X64Reg available[] = { R14, R15 };
    int num_available = 2;
    
    for (int r = 0; r < num_available; r++) {
        int best_slot = -1;
        int best_count = 0;
        
        for (int s = 0; s < max_slots; s++) {
            if (use_count[s] > best_count && slot_to_reg[s] == REG_NONE) {
                best_slot = s;
                best_count = use_count[s];
            }
        }
        
        if (best_slot >= 0 && best_count >= 3) {  // Only if used at least 3 times
            slot_to_reg[best_slot] = available[r];
        }
    }
    
    free(use_count);
}
