#ifndef X64_EMIT_H
#define X64_EMIT_H

// ============================================================================
// TULPAR JIT - x64 Instruction Emitter
// ============================================================================

#include "jit.h"
#include <stdint.h>

// ============================================================================
// x64 REGISTER ENCODING
// ============================================================================

// General purpose registers (64-bit)
typedef enum {
    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,
    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,
    R8  = 8,
    R9  = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15
} X64Reg;

// ============================================================================
// x64 CALLING CONVENTION
// ============================================================================

#ifdef _WIN32
// Windows x64: RCX, RDX, R8, R9 for first 4 args
#define ARG1_REG RCX
#define ARG2_REG RDX
#define ARG3_REG R8
#define ARG4_REG R9
#define SCRATCH_REG R10
#define SCRATCH_REG2 R11
#else
// System V AMD64 (Linux/macOS): RDI, RSI, RDX, RCX, R8, R9
#define ARG1_REG RDI
#define ARG2_REG RSI
#define ARG3_REG RDX
#define ARG4_REG RCX
#define SCRATCH_REG R10
#define SCRATCH_REG2 R11
#endif

// ============================================================================
// REX PREFIX HELPERS
// ============================================================================

// REX prefix bits
#define REX_BASE 0x40
#define REX_W    0x08  // 64-bit operand size
#define REX_R    0x04  // Extension of ModR/M reg field
#define REX_X    0x02  // Extension of SIB index field
#define REX_B    0x01  // Extension of ModR/M r/m, SIB base, or opcode reg

// Check if register needs REX.B
#define NEEDS_REX_B(reg) ((reg) >= R8)
#define NEEDS_REX_R(reg) ((reg) >= R8)

// Get low 3 bits of register
#define REG_LOW(reg) ((reg) & 0x7)

// ============================================================================
// MOV INSTRUCTIONS
// ============================================================================

// mov reg, imm64 (10 bytes)
void x64_mov_reg_imm64(JITCode *code, X64Reg dst, int64_t imm);

// mov reg, imm32 (sign-extended to 64-bit)
void x64_mov_reg_imm32(JITCode *code, X64Reg dst, int32_t imm);

// mov reg1, reg2
void x64_mov_reg_reg(JITCode *code, X64Reg dst, X64Reg src);

// mov reg, [base + offset]
void x64_mov_reg_mem(JITCode *code, X64Reg dst, X64Reg base, int32_t offset);

// mov [base + offset], reg
void x64_mov_mem_reg(JITCode *code, X64Reg base, int32_t offset, X64Reg src);

// mov [base + offset], imm32
void x64_mov_mem_imm32(JITCode *code, X64Reg base, int32_t offset, int32_t imm);

// ============================================================================
// ARITHMETIC INSTRUCTIONS
// ============================================================================

// add reg1, reg2
void x64_add_reg_reg(JITCode *code, X64Reg dst, X64Reg src);

// add reg, imm32
void x64_add_reg_imm32(JITCode *code, X64Reg dst, int32_t imm);

// sub reg1, reg2
void x64_sub_reg_reg(JITCode *code, X64Reg dst, X64Reg src);

// sub reg, imm32
void x64_sub_reg_imm32(JITCode *code, X64Reg dst, int32_t imm);

// imul reg1, reg2 (signed multiply)
void x64_imul_reg_reg(JITCode *code, X64Reg dst, X64Reg src);

// idiv reg (signed divide: RDX:RAX / reg -> RAX quotient, RDX remainder)
void x64_idiv_reg(JITCode *code, X64Reg divisor);

// cqo (sign extend RAX to RDX:RAX for division)
void x64_cqo(JITCode *code);

// neg reg
void x64_neg_reg(JITCode *code, X64Reg reg);

// inc reg
void x64_inc_reg(JITCode *code, X64Reg reg);

// dec reg
void x64_dec_reg(JITCode *code, X64Reg reg);

// ============================================================================
// COMPARISON INSTRUCTIONS
// ============================================================================

// cmp reg1, reg2
void x64_cmp_reg_reg(JITCode *code, X64Reg reg1, X64Reg reg2);

// cmp reg, imm32
void x64_cmp_reg_imm32(JITCode *code, X64Reg reg, int32_t imm);

// cmp [base + offset], imm8
void x64_cmp_mem_imm8(JITCode *code, X64Reg base, int32_t offset, int8_t imm);

// test reg, reg (for zero check)
void x64_test_reg_reg(JITCode *code, X64Reg reg1, X64Reg reg2);

// setl reg (set if less, signed)
void x64_setl_reg(JITCode *code, X64Reg dst);

// setg reg (set if greater, signed)
void x64_setg_reg(JITCode *code, X64Reg dst);

// sete reg (set if equal)
void x64_sete_reg(JITCode *code, X64Reg dst);

// setne reg (set if not equal)
void x64_setne_reg(JITCode *code, X64Reg dst);

// setle reg (set if less or equal)
void x64_setle_reg(JITCode *code, X64Reg dst);

// setge reg (set if greater or equal)
void x64_setge_reg(JITCode *code, X64Reg dst);

// movzx reg, reg8 (zero extend byte to qword)
void x64_movzx_reg_reg8(JITCode *code, X64Reg dst, X64Reg src);

// ============================================================================
// JUMP INSTRUCTIONS
// ============================================================================

// jmp rel32 (returns patch offset)
size_t x64_jmp_rel32(JITCode *code);

// jmp to absolute address (via register)
void x64_jmp_reg(JITCode *code, X64Reg reg);

// je rel32 (jump if equal, returns patch offset)
size_t x64_je_rel32(JITCode *code);

// jne rel32 (jump if not equal)
size_t x64_jne_rel32(JITCode *code);

// jl rel32 (jump if less, signed)
size_t x64_jl_rel32(JITCode *code);

// jle rel32 (jump if less or equal)
size_t x64_jle_rel32(JITCode *code);

// jg rel32 (jump if greater)
size_t x64_jg_rel32(JITCode *code);

// jge rel32 (jump if greater or equal)
size_t x64_jge_rel32(JITCode *code);

// jz rel32 (jump if zero)
size_t x64_jz_rel32(JITCode *code);

// jnz rel32 (jump if not zero)
size_t x64_jnz_rel32(JITCode *code);

// Patch a jump target (offset is from x64_j*_rel32 return value)
void x64_patch_jump(JITCode *code, size_t jump_offset, size_t target);

// ============================================================================
// FUNCTION CALL INSTRUCTIONS
// ============================================================================

// call reg
void x64_call_reg(JITCode *code, X64Reg reg);

// call rel32 (returns patch offset for later patching)
size_t x64_call_rel32(JITCode *code);

// ret
void x64_ret(JITCode *code);

// ============================================================================
// STACK INSTRUCTIONS
// ============================================================================

// push reg
void x64_push_reg(JITCode *code, X64Reg reg);

// pop reg
void x64_pop_reg(JITCode *code, X64Reg reg);

// ============================================================================
// LOGICAL INSTRUCTIONS
// ============================================================================

// and reg1, reg2
void x64_and_reg_reg(JITCode *code, X64Reg dst, X64Reg src);

// or reg1, reg2
void x64_or_reg_reg(JITCode *code, X64Reg dst, X64Reg src);

// xor reg1, reg2
void x64_xor_reg_reg(JITCode *code, X64Reg dst, X64Reg src);

// not reg
void x64_not_reg(JITCode *code, X64Reg reg);

// ============================================================================
// LEA INSTRUCTION
// ============================================================================

// lea reg, [base + offset]
void x64_lea_reg_mem(JITCode *code, X64Reg dst, X64Reg base, int32_t offset);

// ============================================================================
// NOP
// ============================================================================

void x64_nop(JITCode *code);

#endif // X64_EMIT_H
