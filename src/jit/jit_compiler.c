// ============================================================================
// TULPAR JIT - Bytecode to Native Code Compiler
// ============================================================================

#include "jit.h"
#include "x64_emit.h"
#include "../vm/vm.h"
#include "../vm/bytecode.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// External optimization functions
extern int jit_try_constant_fold(uint8_t *ip, Chunk *chunk, int64_t *result);

// ============================================================================
// JIT COMPILER STATE
// ============================================================================

typedef struct {
    JITCode *code;
    VM *vm;
    ObjFunction *func;
    Chunk *chunk;
    
    // Bytecode to native offset mapping (for jumps)
    size_t *bc_to_native;   // Maps bytecode offset -> native code offset
    int bc_length;
    
    // Pending jumps to patch
    struct {
        size_t native_offset;   // Where the jump was emitted
        int bc_target;          // Target bytecode offset
    } pending_jumps[256];
    int pending_jump_count;
    
    // Stack simulation for register allocation
    int stack_depth;
    
} JITCompilerState;

// ============================================================================
// REGISTER ALLOCATION STRATEGY
// ============================================================================
// 
// We use a simple strategy:
// - VM struct pointer passed in ARG1_REG (RCX on Windows, RDI on Linux)
// - Stack top pointer kept in R12 (callee-saved)
// - Scratch registers: RAX, RBX, R10, R11
// - All values go through the VM stack (for now - full register allocation later)
//
// VMValue layout (16 bytes):
//   offset 0: type (4 bytes, padded)
//   offset 8: value union (8 bytes)

#define VM_REG      ARG1_REG     // VM pointer passed as first argument
#define STACK_REG   R12         // Cached stack_top pointer (callee-saved)
#define SLOTS_REG   R13         // Cached frame->slots pointer (callee-saved)

#define VMVALUE_SIZE 16
#define VMVALUE_TYPE_OFFSET 0
#define VMVALUE_VAL_OFFSET 8

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void jit_emit_prologue(JITCompilerState *state) {
    JITCode *code = state->code;
    
    // Save callee-saved registers
    x64_push_reg(code, RBP);
    x64_mov_reg_reg(code, RBP, RSP);
    x64_push_reg(code, RBX);
    x64_push_reg(code, R12);
    x64_push_reg(code, R13);
    x64_push_reg(code, R14);
    x64_push_reg(code, R15);
    
    // Align stack to 16 bytes if needed
    x64_sub_reg_imm32(code, RSP, 8);
    
    // Load VM->stack_top into STACK_REG
    // stack_top is at offsetof(VM, stack_top)
    // VM struct layout: frames[256*sizeof(CallFrame)] + frame_count(4) + stack[4096*16] + stack_top(8)
    // Simpler: just calculate offset
    int stack_top_offset = (int)((char*)&((VM*)0)->stack_top - (char*)0);
    x64_mov_reg_mem(code, STACK_REG, VM_REG, stack_top_offset);
    
    // Load frame->slots into SLOTS_REG
    // For now, assume slots = stack base (first slot)
    // In practice, this would come from the current frame
    int stack_offset = (int)((char*)&((VM*)0)->stack - (char*)0);
    x64_lea_reg_mem(code, SLOTS_REG, VM_REG, stack_offset);
}

static void jit_emit_epilogue(JITCompilerState *state) {
    JITCode *code = state->code;
    
    // Store stack_top back to VM
    int stack_top_offset = (int)((char*)&((VM*)0)->stack_top - (char*)0);
    x64_mov_mem_reg(code, VM_REG, stack_top_offset, STACK_REG);
    
    // Restore stack alignment
    x64_add_reg_imm32(code, RSP, 8);
    
    // Restore callee-saved registers
    x64_pop_reg(code, R15);
    x64_pop_reg(code, R14);
    x64_pop_reg(code, R13);
    x64_pop_reg(code, R12);
    x64_pop_reg(code, RBX);
    x64_pop_reg(code, RBP);
    
    x64_ret(code);
}

// Push value from RAX (type in EAX low bits, value in RBX)
static void jit_emit_push_int(JITCompilerState *state, X64Reg val_reg) {
    JITCode *code = state->code;
    
    // Store type (VM_VAL_INT = 0)
    x64_mov_mem_imm32(code, STACK_REG, VMVALUE_TYPE_OFFSET, 0);
    
    // Store value
    x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, val_reg);
    
    // Advance stack top
    x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
    
    state->stack_depth++;
}

// Pop value into RAX (returns value, type in memory)
static void jit_emit_pop_int(JITCompilerState *state, X64Reg dst_reg) {
    JITCode *code = state->code;
    
    // Retreat stack top
    x64_sub_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
    
    // Load value (assuming it's an int)
    x64_mov_reg_mem(code, dst_reg, STACK_REG, VMVALUE_VAL_OFFSET);
    
    state->stack_depth--;
}

// Peek value at offset from top
static void jit_emit_peek_int(JITCompilerState *state, X64Reg dst_reg, int offset) {
    JITCode *code = state->code;
    
    // Calculate offset from current stack top
    int byte_offset = -(offset + 1) * VMVALUE_SIZE + VMVALUE_VAL_OFFSET;
    x64_mov_reg_mem(code, dst_reg, STACK_REG, byte_offset);
}

// Record current position for bytecode offset
static void jit_record_bc_offset(JITCompilerState *state, int bc_offset) {
    if (bc_offset < state->bc_length) {
        state->bc_to_native[bc_offset] = jit_code_pos(state->code);
    }
}

// Add pending jump to patch later
static void jit_add_pending_jump(JITCompilerState *state, size_t native_offset, int bc_target) {
    if (state->pending_jump_count < 256) {
        state->pending_jumps[state->pending_jump_count].native_offset = native_offset;
        state->pending_jumps[state->pending_jump_count].bc_target = bc_target;
        state->pending_jump_count++;
    }
}

// Patch all pending jumps
static void jit_patch_jumps(JITCompilerState *state) {
    for (int i = 0; i < state->pending_jump_count; i++) {
        size_t native_offset = state->pending_jumps[i].native_offset;
        int bc_target = state->pending_jumps[i].bc_target;
        
        if (bc_target < state->bc_length && state->bc_to_native[bc_target] != 0) {
            x64_patch_jump(state->code, native_offset, state->bc_to_native[bc_target]);
        }
    }
}

// ============================================================================
// TYPE SPECIALIZATION - Guard Instructions
// ============================================================================

// Emit type guard - check if value at stack offset has expected type
// If not, jump to deoptimization point (for now, we'll just continue to interpreter)
static void jit_emit_type_guard_int(JITCompilerState *state, int stack_offset) {
    JITCode *code = state->code;
    
    // Load type from stack[top - offset - 1]
    int byte_offset = -(stack_offset + 1) * VMVALUE_SIZE + VMVALUE_TYPE_OFFSET;
    x64_cmp_mem_imm8(code, STACK_REG, byte_offset, 0);  // VM_VAL_INT = 0
    
    // If not int, we would jump to deopt - for now just continue
    // TODO: Add deoptimization path
}

// Specialized ADD for int+int (no type checking in hot path)
static void jit_emit_add_int_specialized(JITCompilerState *state) {
    JITCode *code = state->code;
    
    // Direct register-based add without stack manipulation
    // Load a and b directly, compute, store result
    
    // Load b (top of stack)
    x64_mov_reg_mem(code, RBX, STACK_REG, -VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
    // Load a (second from top)
    x64_mov_reg_mem(code, RAX, STACK_REG, -2 * VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
    
    // Add
    x64_add_reg_reg(code, RAX, RBX);
    
    // Store result back to a's position (we'll pop b)
    x64_mov_mem_reg(code, STACK_REG, -2 * VMVALUE_SIZE + VMVALUE_VAL_OFFSET, RAX);
    // Type is already int, no need to update
    
    // Pop one element
    x64_sub_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
    state->stack_depth--;
}

// Specialized SUB for int-int
static void jit_emit_sub_int_specialized(JITCompilerState *state) {
    JITCode *code = state->code;
    
    x64_mov_reg_mem(code, RBX, STACK_REG, -VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
    x64_mov_reg_mem(code, RAX, STACK_REG, -2 * VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
    x64_sub_reg_reg(code, RAX, RBX);
    x64_mov_mem_reg(code, STACK_REG, -2 * VMVALUE_SIZE + VMVALUE_VAL_OFFSET, RAX);
    x64_sub_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
    state->stack_depth--;
}

// Specialized MUL for int*int
static void jit_emit_mul_int_specialized(JITCompilerState *state) {
    JITCode *code = state->code;
    
    x64_mov_reg_mem(code, RBX, STACK_REG, -VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
    x64_mov_reg_mem(code, RAX, STACK_REG, -2 * VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
    x64_imul_reg_reg(code, RAX, RBX);
    x64_mov_mem_reg(code, STACK_REG, -2 * VMVALUE_SIZE + VMVALUE_VAL_OFFSET, RAX);
    x64_sub_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
    state->stack_depth--;
}

// Specialized comparison for int < int
static void jit_emit_lt_int_specialized(JITCompilerState *state) {
    JITCode *code = state->code;
    
    x64_mov_reg_mem(code, RBX, STACK_REG, -VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
    x64_mov_reg_mem(code, RAX, STACK_REG, -2 * VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
    x64_cmp_reg_reg(code, RAX, RBX);
    x64_setl_reg(code, RAX);
    x64_movzx_reg_reg8(code, RAX, RAX);
    
    // Store as bool
    x64_mov_mem_imm32(code, STACK_REG, -2 * VMVALUE_SIZE + VMVALUE_TYPE_OFFSET, 2);
    x64_mov_mem_reg(code, STACK_REG, -2 * VMVALUE_SIZE + VMVALUE_VAL_OFFSET, RAX);
    x64_sub_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
    state->stack_depth--;
}

// ============================================================================
// OPCODE COMPILATION
// ============================================================================

static int jit_compile_opcode(JITCompilerState *state, uint8_t **ip_ptr) {
    JITCode *code = state->code;
    uint8_t *ip = *ip_ptr;
    uint8_t opcode = *ip++;
    
    // Record bytecode position
    int bc_offset = (int)(ip - 1 - state->chunk->code);
    jit_record_bc_offset(state, bc_offset);
    
    switch (opcode) {
        case OP_NOP:
            x64_nop(code);
            break;
            
        case OP_CONST_INT: {
            // Try constant folding first
            int64_t folded_result;
            int fold_bytes = jit_try_constant_fold(ip - 1, state->chunk, &folded_result);
            if (fold_bytes > 0) {
                // Skip the folded bytecodes, emit result directly
                ip += fold_bytes - 1;  // -1 because we already read OP_CONST_INT
                x64_mov_reg_imm64(code, RAX, folded_result);
                jit_emit_push_int(state, RAX);
                break;
            }
            
            // Normal path - read constant index
            uint16_t idx = ip[0] | (ip[1] << 8);
            ip += 2;
            
            Constant c = state->chunk->constants[idx];
            int64_t val = c.int_val;
            
            // Load immediate into RAX and push
            x64_mov_reg_imm64(code, RAX, val);
            jit_emit_push_int(state, RAX);
            break;
        }
        
        case OP_CONST_TRUE:
            // Push 1 as bool (type = VM_VAL_BOOL = 2)
            x64_mov_mem_imm32(code, STACK_REG, VMVALUE_TYPE_OFFSET, 2);
            x64_mov_reg_imm32(code, RAX, 1);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
            
        case OP_CONST_FALSE:
            x64_mov_mem_imm32(code, STACK_REG, VMVALUE_TYPE_OFFSET, 2);
            x64_mov_reg_imm32(code, RAX, 0);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
            
        case OP_POP:
            x64_sub_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth--;
            break;
            
        case OP_DUP:
            // Copy top value
            jit_emit_peek_int(state, RAX, 0);
            x64_mov_reg_mem(code, RBX, STACK_REG, -VMVALUE_SIZE + VMVALUE_TYPE_OFFSET);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_TYPE_OFFSET, RBX);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
            
        case OP_ADD:
            // Use specialized int+int path (assumes both are int)
            jit_emit_add_int_specialized(state);
            break;
            
        case OP_SUB:
            // Use specialized int-int path
            jit_emit_sub_int_specialized(state);
            break;
            
        case OP_MUL:
            // Use specialized int*int path
            jit_emit_mul_int_specialized(state);
            break;
            
        case OP_DIV:
            jit_emit_pop_int(state, RBX);  // b (divisor)
            jit_emit_pop_int(state, RAX);  // a (dividend)
            x64_cqo(code);                  // Sign extend RAX to RDX:RAX
            x64_idiv_reg(code, RBX);        // RAX = quotient
            jit_emit_push_int(state, RAX);
            break;
            
        case OP_MOD:
            jit_emit_pop_int(state, RBX);  // b
            jit_emit_pop_int(state, RAX);  // a
            x64_cqo(code);
            x64_idiv_reg(code, RBX);
            // Remainder is in RDX
            x64_mov_reg_reg(code, RAX, RDX);
            jit_emit_push_int(state, RAX);
            break;
            
        case OP_NEG:
            jit_emit_pop_int(state, RAX);
            x64_neg_reg(code, RAX);
            jit_emit_push_int(state, RAX);
            break;
            
        case OP_INC:
            jit_emit_pop_int(state, RAX);
            x64_inc_reg(code, RAX);
            jit_emit_push_int(state, RAX);
            break;
            
        case OP_DEC:
            jit_emit_pop_int(state, RAX);
            x64_dec_reg(code, RAX);
            jit_emit_push_int(state, RAX);
            break;
            
        case OP_LT:
            // Use specialized int<int path
            jit_emit_lt_int_specialized(state);
            break;
            
        case OP_LE:
            jit_emit_pop_int(state, RBX);
            jit_emit_pop_int(state, RAX);
            x64_cmp_reg_reg(code, RAX, RBX);
            x64_setle_reg(code, RAX);
            x64_movzx_reg_reg8(code, RAX, RAX);
            x64_mov_mem_imm32(code, STACK_REG, VMVALUE_TYPE_OFFSET, 2);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
            
        case OP_GT:
            jit_emit_pop_int(state, RBX);
            jit_emit_pop_int(state, RAX);
            x64_cmp_reg_reg(code, RAX, RBX);
            x64_setg_reg(code, RAX);
            x64_movzx_reg_reg8(code, RAX, RAX);
            x64_mov_mem_imm32(code, STACK_REG, VMVALUE_TYPE_OFFSET, 2);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
            
        case OP_GE:
            jit_emit_pop_int(state, RBX);
            jit_emit_pop_int(state, RAX);
            x64_cmp_reg_reg(code, RAX, RBX);
            x64_setge_reg(code, RAX);
            x64_movzx_reg_reg8(code, RAX, RAX);
            x64_mov_mem_imm32(code, STACK_REG, VMVALUE_TYPE_OFFSET, 2);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
            
        case OP_EQ:
            jit_emit_pop_int(state, RBX);
            jit_emit_pop_int(state, RAX);
            x64_cmp_reg_reg(code, RAX, RBX);
            x64_sete_reg(code, RAX);
            x64_movzx_reg_reg8(code, RAX, RAX);
            x64_mov_mem_imm32(code, STACK_REG, VMVALUE_TYPE_OFFSET, 2);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
            
        case OP_NE:
            jit_emit_pop_int(state, RBX);
            jit_emit_pop_int(state, RAX);
            x64_cmp_reg_reg(code, RAX, RBX);
            x64_setne_reg(code, RAX);
            x64_movzx_reg_reg8(code, RAX, RAX);
            x64_mov_mem_imm32(code, STACK_REG, VMVALUE_TYPE_OFFSET, 2);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
            
        case OP_NOT:
            jit_emit_pop_int(state, RAX);
            x64_test_reg_reg(code, RAX, RAX);
            x64_sete_reg(code, RAX);  // Set to 1 if was 0
            x64_movzx_reg_reg8(code, RAX, RAX);
            x64_mov_mem_imm32(code, STACK_REG, VMVALUE_TYPE_OFFSET, 2);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
            
        case OP_LOAD_LOCAL: {
            uint16_t slot = ip[0] | (ip[1] << 8);
            ip += 2;
            
            // Load from slots[slot]
            int offset = slot * VMVALUE_SIZE;
            
            // Copy type
            x64_mov_reg_mem(code, RAX, SLOTS_REG, offset + VMVALUE_TYPE_OFFSET);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_TYPE_OFFSET, RAX);
            
            // Copy value
            x64_mov_reg_mem(code, RAX, SLOTS_REG, offset + VMVALUE_VAL_OFFSET);
            x64_mov_mem_reg(code, STACK_REG, VMVALUE_VAL_OFFSET, RAX);
            
            x64_add_reg_imm32(code, STACK_REG, VMVALUE_SIZE);
            state->stack_depth++;
            break;
        }
        
        case OP_STORE_LOCAL: {
            uint16_t slot = ip[0] | (ip[1] << 8);
            ip += 2;
            
            int offset = slot * VMVALUE_SIZE;
            
            // Peek top (don't pop)
            x64_mov_reg_mem(code, RAX, STACK_REG, -VMVALUE_SIZE + VMVALUE_TYPE_OFFSET);
            x64_mov_mem_reg(code, SLOTS_REG, offset + VMVALUE_TYPE_OFFSET, RAX);
            
            x64_mov_reg_mem(code, RAX, STACK_REG, -VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
            x64_mov_mem_reg(code, SLOTS_REG, offset + VMVALUE_VAL_OFFSET, RAX);
            break;
        }
        
        case OP_JUMP: {
            uint16_t offset = ip[0] | (ip[1] << 8);
            ip += 2;
            
            int bc_target = (int)(ip - state->chunk->code) + offset;
            size_t jump_offset = x64_jmp_rel32(code);
            jit_add_pending_jump(state, jump_offset, bc_target);
            break;
        }
        
        case OP_JUMP_IF_FALSE: {
            uint16_t offset = ip[0] | (ip[1] << 8);
            ip += 2;
            
            // Peek top value
            x64_mov_reg_mem(code, RAX, STACK_REG, -VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
            x64_test_reg_reg(code, RAX, RAX);
            
            int bc_target = (int)(ip - state->chunk->code) + offset;
            size_t jump_offset = x64_jz_rel32(code);
            jit_add_pending_jump(state, jump_offset, bc_target);
            break;
        }
        
        case OP_JUMP_IF_TRUE: {
            uint16_t offset = ip[0] | (ip[1] << 8);
            ip += 2;
            
            x64_mov_reg_mem(code, RAX, STACK_REG, -VMVALUE_SIZE + VMVALUE_VAL_OFFSET);
            x64_test_reg_reg(code, RAX, RAX);
            
            int bc_target = (int)(ip - state->chunk->code) + offset;
            size_t jump_offset = x64_jnz_rel32(code);
            jit_add_pending_jump(state, jump_offset, bc_target);
            break;
        }
        
        case OP_LOOP: {
            uint16_t offset = ip[0] | (ip[1] << 8);
            ip += 2;
            
            int bc_target = (int)(ip - state->chunk->code) - offset;
            size_t jump_offset = x64_jmp_rel32(code);
            jit_add_pending_jump(state, jump_offset, bc_target);
            break;
        }
        
        case OP_CALL: {
            // Function call via interpreter fallback
            uint8_t arg_count = *ip++;
            
            // Save stack_top back to VM before calling helper
            int stack_top_offset = (int)((char*)&((VM*)0)->stack_top - (char*)0);
            x64_mov_mem_reg(code, VM_REG, stack_top_offset, STACK_REG);
            
            // Call jit_helper_call(vm, arg_count)
            // Windows: RCX = vm (already), RDX = arg_count
            // Linux: RDI = vm (already), RSI = arg_count
            x64_mov_reg_imm32(code, ARG2_REG, arg_count);
            
            // Get address of jit_helper_call
            extern void jit_helper_call(VM *vm, int arg_count);
            x64_mov_reg_imm64(code, RAX, (int64_t)(uintptr_t)jit_helper_call);
            
            // Align stack for call (already aligned in prologue)
            x64_call_reg(code, RAX);
            
            // Reload stack_top from VM after call
            x64_mov_reg_mem(code, STACK_REG, VM_REG, stack_top_offset);
            
            // Update stack depth simulation
            state->stack_depth -= arg_count;  // Args consumed
            // Result pushed by helper, but we already accounted for callee
            break;
        }
        
        case OP_CALL_FAST: {
            uint8_t arg_count = *ip++;
            
            int stack_top_offset = (int)((char*)&((VM*)0)->stack_top - (char*)0);
            x64_mov_mem_reg(code, VM_REG, stack_top_offset, STACK_REG);
            
            x64_mov_reg_imm32(code, ARG2_REG, arg_count);
            extern void jit_helper_call(VM *vm, int arg_count);
            x64_mov_reg_imm64(code, RAX, (int64_t)(uintptr_t)jit_helper_call);
            x64_call_reg(code, RAX);
            
            x64_mov_reg_mem(code, STACK_REG, VM_REG, stack_top_offset);
            state->stack_depth -= arg_count;
            break;
        }
        
        case OP_CALL_0: {
            int stack_top_offset = (int)((char*)&((VM*)0)->stack_top - (char*)0);
            x64_mov_mem_reg(code, VM_REG, stack_top_offset, STACK_REG);
            
            x64_mov_reg_imm32(code, ARG2_REG, 0);
            extern void jit_helper_call(VM *vm, int arg_count);
            x64_mov_reg_imm64(code, RAX, (int64_t)(uintptr_t)jit_helper_call);
            x64_call_reg(code, RAX);
            
            x64_mov_reg_mem(code, STACK_REG, VM_REG, stack_top_offset);
            break;
        }
        
        case OP_CALL_1: {
            int stack_top_offset = (int)((char*)&((VM*)0)->stack_top - (char*)0);
            x64_mov_mem_reg(code, VM_REG, stack_top_offset, STACK_REG);
            
            x64_mov_reg_imm32(code, ARG2_REG, 1);
            extern void jit_helper_call(VM *vm, int arg_count);
            x64_mov_reg_imm64(code, RAX, (int64_t)(uintptr_t)jit_helper_call);
            x64_call_reg(code, RAX);
            
            x64_mov_reg_mem(code, STACK_REG, VM_REG, stack_top_offset);
            state->stack_depth--;
            break;
        }
        
        case OP_CALL_2: {
            int stack_top_offset = (int)((char*)&((VM*)0)->stack_top - (char*)0);
            x64_mov_mem_reg(code, VM_REG, stack_top_offset, STACK_REG);
            
            x64_mov_reg_imm32(code, ARG2_REG, 2);
            extern void jit_helper_call(VM *vm, int arg_count);
            x64_mov_reg_imm64(code, RAX, (int64_t)(uintptr_t)jit_helper_call);
            x64_call_reg(code, RAX);
            
            x64_mov_reg_mem(code, STACK_REG, VM_REG, stack_top_offset);
            state->stack_depth -= 2;
            break;
        }
        
        case OP_RETURN:
        case OP_RETURN_FAST:
        case OP_RETURN_VOID:
        case OP_HALT:
            // For now, just return from JIT code
            // The interpreter will handle the actual return
            jit_emit_epilogue(state);
            *ip_ptr = ip;
            return 0;  // Stop compilation
            
        default:
            // Unsupported opcode - bail out
            fprintf(stderr, "JIT: Unsupported opcode %d at offset %d\n", 
                    opcode, bc_offset);
            *ip_ptr = ip;
            return -1;
    }
    
    *ip_ptr = ip;
    return 1;  // Continue compilation
}

// ============================================================================
// PRE-COMPILATION ANALYSIS
// ============================================================================

// Check if function can be JIT compiled
// Now supports OP_CALL via interpreter fallback
static int jit_can_compile(ObjFunction *func) {
    uint8_t *ip = func->chunk.code;
    uint8_t *end = ip + func->chunk.code_length;
    
    while (ip < end) {
        uint8_t op = *ip;
        
        // Check for unsupported opcodes
        switch (op) {
            // OP_CALL is now supported via interpreter fallback!
            case OP_TAIL_CALL:
            case OP_CALL_BUILTIN:
                // These still need interpreter
                return 0;
                
            case OP_CONST_STR:
            case OP_PRINT:
            case OP_IMPORT:
            case OP_TRY:
            case OP_POP_TRY:
            case OP_THROW:
            case OP_ARRAY_NEW:
            case OP_ARRAY_PUSH:
            case OP_ARRAY_GET:
            case OP_ARRAY_SET:
            case OP_OBJECT_NEW:
            case OP_OBJECT_GET:
            case OP_OBJECT_SET:
                // Complex opcodes - use interpreter
                return 0;
                
            // Skip over operands
            case OP_CONST_INT:
            case OP_CONST_FLOAT:
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
                ip += 3;  // opcode + 2 byte operand
                break;
            
            case OP_CALL:
            case OP_CALL_FAST:
                ip += 2;  // opcode + arg_count
                break;
                
            case OP_CALL_0:
            case OP_CALL_1:
            case OP_CALL_2:
                ip += 1;  // just opcode
                break;
                
            default:
                ip++;
                break;
        }
    }
    
    return 1;  // Safe to JIT compile
}

// ============================================================================
// MAIN COMPILATION ENTRY POINT
// ============================================================================

JITCompiledFunc *jit_compile_function(VM *vm, ObjFunction *func) {
    if (!func || !func->chunk.code || func->chunk.code_length == 0) {
        return NULL;
    }
    
    // Check if function can be JIT compiled
    if (!jit_can_compile(func)) {
        // Function contains unsupported opcodes, use interpreter
        return NULL;
    }
    
    // Allocate state
    JITCompilerState state;
    memset(&state, 0, sizeof(state));
    
    state.code = jit_code_alloc(JIT_CODE_SIZE_INITIAL);
    if (!state.code) {
        return NULL;
    }
    
    state.vm = vm;
    state.func = func;
    state.chunk = &func->chunk;
    state.bc_length = func->chunk.code_length;
    state.stack_depth = 0;
    state.pending_jump_count = 0;
    
    // Allocate bytecode-to-native mapping
    state.bc_to_native = (size_t *)calloc(state.bc_length, sizeof(size_t));
    if (!state.bc_to_native) {
        jit_code_free(state.code);
        return NULL;
    }
    
    // Emit prologue
    jit_emit_prologue(&state);
    
    // Compile bytecode
    uint8_t *ip = func->chunk.code;
    uint8_t *end = ip + func->chunk.code_length;
    
    while (ip < end) {
        int result = jit_compile_opcode(&state, &ip);
        if (result < 0) {
            // Compilation failed
            free(state.bc_to_native);
            jit_code_free(state.code);
            return NULL;
        }
        if (result == 0) {
            // Hit return/halt
            break;
        }
    }
    
    // Patch all jumps
    jit_patch_jumps(&state);
    
    // Make code executable
    if (!jit_code_make_executable(state.code)) {
        free(state.bc_to_native);
        jit_code_free(state.code);
        return NULL;
    }
    
    // Create result
    JITCompiledFunc *result = (JITCompiledFunc *)malloc(sizeof(JITCompiledFunc));
    if (!result) {
        free(state.bc_to_native);
        jit_code_free(state.code);
        return NULL;
    }
    
    result->entry = (JITFunction)state.code->code;
    result->code = state.code;
    result->valid = 1;
    
    free(state.bc_to_native);
    
    return result;
}

// ============================================================================
// PROFILER
// ============================================================================

int jit_should_compile(ObjFunction *func) {
    return func->call_count >= JIT_THRESHOLD;
}

int jit_profile_call(ObjFunction *func) {
    func->call_count++;
    return func->call_count >= JIT_THRESHOLD;
}
