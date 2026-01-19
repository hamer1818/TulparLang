// ============================================================================
// TULPAR JIT - x64 Instruction Emitter Implementation
// ============================================================================

#include "x64_emit.h"
#include <string.h>

// ============================================================================
// HELPER MACROS
// ============================================================================

// Emit a single byte
#define EMIT(b) jit_emit_byte(code, (b))

// Emit REX prefix if needed for 64-bit operation
#define EMIT_REX_W(dst, src) do { \
    uint8_t rex = REX_BASE | REX_W; \
    if (NEEDS_REX_R(dst)) rex |= REX_R; \
    if (NEEDS_REX_B(src)) rex |= REX_B; \
    EMIT(rex); \
} while(0)

// ModR/M byte: mod=11 (register direct), reg, r/m
#define MODRM_REG(reg, rm) (0xC0 | (REG_LOW(reg) << 3) | REG_LOW(rm))

// ModR/M byte: mod=00 (no displacement), reg, r/m
#define MODRM_MEM0(reg, rm) (0x00 | (REG_LOW(reg) << 3) | REG_LOW(rm))

// ModR/M byte: mod=01 (8-bit displacement), reg, r/m
#define MODRM_MEM8(reg, rm) (0x40 | (REG_LOW(reg) << 3) | REG_LOW(rm))

// ModR/M byte: mod=10 (32-bit displacement), reg, r/m
#define MODRM_MEM32(reg, rm) (0x80 | (REG_LOW(reg) << 3) | REG_LOW(rm))

// Emit 32-bit immediate (little endian)
static void emit_imm32(JITCode *code, int32_t imm) {
    EMIT(imm & 0xFF);
    EMIT((imm >> 8) & 0xFF);
    EMIT((imm >> 16) & 0xFF);
    EMIT((imm >> 24) & 0xFF);
}

// Emit 64-bit immediate (little endian)
static void emit_imm64(JITCode *code, int64_t imm) {
    EMIT(imm & 0xFF);
    EMIT((imm >> 8) & 0xFF);
    EMIT((imm >> 16) & 0xFF);
    EMIT((imm >> 24) & 0xFF);
    EMIT((imm >> 32) & 0xFF);
    EMIT((imm >> 40) & 0xFF);
    EMIT((imm >> 48) & 0xFF);
    EMIT((imm >> 56) & 0xFF);
}

// Emit memory operand with displacement
static void emit_mem_operand(JITCode *code, X64Reg reg, X64Reg base, int32_t offset) {
    // Special handling for RSP/R12 (need SIB byte)
    int need_sib = (REG_LOW(base) == REG_LOW(RSP));
    
    if (offset == 0 && REG_LOW(base) != REG_LOW(RBP)) {
        // [base] - no displacement (except RBP needs disp8=0)
        if (need_sib) {
            EMIT(MODRM_MEM0(reg, RSP));
            EMIT(0x24);  // SIB: scale=1, index=RSP (none), base=RSP
        } else {
            EMIT(MODRM_MEM0(reg, base));
        }
    } else if (offset >= -128 && offset <= 127) {
        // [base + disp8]
        if (need_sib) {
            EMIT(MODRM_MEM8(reg, RSP));
            EMIT(0x24);
        } else {
            EMIT(MODRM_MEM8(reg, base));
        }
        EMIT((int8_t)offset);
    } else {
        // [base + disp32]
        if (need_sib) {
            EMIT(MODRM_MEM32(reg, RSP));
            EMIT(0x24);
        } else {
            EMIT(MODRM_MEM32(reg, base));
        }
        emit_imm32(code, offset);
    }
}

// ============================================================================
// MOV INSTRUCTIONS
// ============================================================================

void x64_mov_reg_imm64(JITCode *code, X64Reg dst, int64_t imm) {
    // REX.W + B8+rd (mov r64, imm64)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(dst)) rex |= REX_B;
    EMIT(rex);
    EMIT(0xB8 + REG_LOW(dst));
    emit_imm64(code, imm);
}

void x64_mov_reg_imm32(JITCode *code, X64Reg dst, int32_t imm) {
    // REX.W + C7 /0 (mov r/m64, imm32 sign-extended)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(dst)) rex |= REX_B;
    EMIT(rex);
    EMIT(0xC7);
    EMIT(MODRM_REG(0, dst));
    emit_imm32(code, imm);
}

void x64_mov_reg_reg(JITCode *code, X64Reg dst, X64Reg src) {
    // REX.W + 89 /r (mov r/m64, r64)
    EMIT_REX_W(src, dst);
    EMIT(0x89);
    EMIT(MODRM_REG(src, dst));
}

void x64_mov_reg_mem(JITCode *code, X64Reg dst, X64Reg base, int32_t offset) {
    // REX.W + 8B /r (mov r64, r/m64)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_R(dst)) rex |= REX_R;
    if (NEEDS_REX_B(base)) rex |= REX_B;
    EMIT(rex);
    EMIT(0x8B);
    emit_mem_operand(code, dst, base, offset);
}

void x64_mov_mem_reg(JITCode *code, X64Reg base, int32_t offset, X64Reg src) {
    // REX.W + 89 /r (mov r/m64, r64)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_R(src)) rex |= REX_R;
    if (NEEDS_REX_B(base)) rex |= REX_B;
    EMIT(rex);
    EMIT(0x89);
    emit_mem_operand(code, src, base, offset);
}

void x64_mov_mem_imm32(JITCode *code, X64Reg base, int32_t offset, int32_t imm) {
    // REX.W + C7 /0 (mov r/m64, imm32)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(base)) rex |= REX_B;
    EMIT(rex);
    EMIT(0xC7);
    emit_mem_operand(code, (X64Reg)0, base, offset);
    emit_imm32(code, imm);
}

// ============================================================================
// ARITHMETIC INSTRUCTIONS
// ============================================================================

void x64_add_reg_reg(JITCode *code, X64Reg dst, X64Reg src) {
    // REX.W + 01 /r (add r/m64, r64)
    EMIT_REX_W(src, dst);
    EMIT(0x01);
    EMIT(MODRM_REG(src, dst));
}

void x64_add_reg_imm32(JITCode *code, X64Reg dst, int32_t imm) {
    // REX.W + 81 /0 (add r/m64, imm32)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(dst)) rex |= REX_B;
    EMIT(rex);
    
    if (imm >= -128 && imm <= 127) {
        // Use shorter form with imm8
        EMIT(0x83);
        EMIT(MODRM_REG(0, dst));
        EMIT((int8_t)imm);
    } else {
        EMIT(0x81);
        EMIT(MODRM_REG(0, dst));
        emit_imm32(code, imm);
    }
}

void x64_sub_reg_reg(JITCode *code, X64Reg dst, X64Reg src) {
    // REX.W + 29 /r (sub r/m64, r64)
    EMIT_REX_W(src, dst);
    EMIT(0x29);
    EMIT(MODRM_REG(src, dst));
}

void x64_sub_reg_imm32(JITCode *code, X64Reg dst, int32_t imm) {
    // REX.W + 81 /5 (sub r/m64, imm32)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(dst)) rex |= REX_B;
    EMIT(rex);
    
    if (imm >= -128 && imm <= 127) {
        EMIT(0x83);
        EMIT(MODRM_REG(5, dst));
        EMIT((int8_t)imm);
    } else {
        EMIT(0x81);
        EMIT(MODRM_REG(5, dst));
        emit_imm32(code, imm);
    }
}

void x64_imul_reg_reg(JITCode *code, X64Reg dst, X64Reg src) {
    // REX.W + 0F AF /r (imul r64, r/m64)
    EMIT_REX_W(dst, src);
    EMIT(0x0F);
    EMIT(0xAF);
    EMIT(MODRM_REG(dst, src));
}

void x64_idiv_reg(JITCode *code, X64Reg divisor) {
    // REX.W + F7 /7 (idiv r/m64)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(divisor)) rex |= REX_B;
    EMIT(rex);
    EMIT(0xF7);
    EMIT(MODRM_REG(7, divisor));
}

void x64_cqo(JITCode *code) {
    // REX.W + 99 (cqo - sign extend RAX to RDX:RAX)
    EMIT(REX_BASE | REX_W);
    EMIT(0x99);
}

void x64_neg_reg(JITCode *code, X64Reg reg) {
    // REX.W + F7 /3 (neg r/m64)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(reg)) rex |= REX_B;
    EMIT(rex);
    EMIT(0xF7);
    EMIT(MODRM_REG(3, reg));
}

void x64_inc_reg(JITCode *code, X64Reg reg) {
    // REX.W + FF /0 (inc r/m64)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(reg)) rex |= REX_B;
    EMIT(rex);
    EMIT(0xFF);
    EMIT(MODRM_REG(0, reg));
}

void x64_dec_reg(JITCode *code, X64Reg reg) {
    // REX.W + FF /1 (dec r/m64)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(reg)) rex |= REX_B;
    EMIT(rex);
    EMIT(0xFF);
    EMIT(MODRM_REG(1, reg));
}

// ============================================================================
// COMPARISON INSTRUCTIONS
// ============================================================================

void x64_cmp_reg_reg(JITCode *code, X64Reg reg1, X64Reg reg2) {
    // REX.W + 39 /r (cmp r/m64, r64)
    EMIT_REX_W(reg2, reg1);
    EMIT(0x39);
    EMIT(MODRM_REG(reg2, reg1));
}

void x64_cmp_reg_imm32(JITCode *code, X64Reg reg, int32_t imm) {
    // REX.W + 81 /7 (cmp r/m64, imm32)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(reg)) rex |= REX_B;
    EMIT(rex);
    
    if (imm >= -128 && imm <= 127) {
        EMIT(0x83);
        EMIT(MODRM_REG(7, reg));
        EMIT((int8_t)imm);
    } else {
        EMIT(0x81);
        EMIT(MODRM_REG(7, reg));
        emit_imm32(code, imm);
    }
}

void x64_cmp_mem_imm8(JITCode *code, X64Reg base, int32_t offset, int8_t imm) {
    // 80 /7 ib (cmp r/m8, imm8)
    if (NEEDS_REX_B(base)) {
        EMIT(REX_BASE | REX_B);
    }
    EMIT(0x80);
    emit_mem_operand(code, (X64Reg)7, base, offset);
    EMIT(imm);
}

void x64_test_reg_reg(JITCode *code, X64Reg reg1, X64Reg reg2) {
    // REX.W + 85 /r (test r/m64, r64)
    EMIT_REX_W(reg2, reg1);
    EMIT(0x85);
    EMIT(MODRM_REG(reg2, reg1));
}

// SETcc instructions
static void emit_setcc(JITCode *code, uint8_t cc, X64Reg dst) {
    // REX + 0F 9x /0 (setcc r/m8)
    if (NEEDS_REX_B(dst)) {
        EMIT(REX_BASE | REX_B);
    }
    EMIT(0x0F);
    EMIT(cc);
    EMIT(MODRM_REG(0, dst));
}

void x64_setl_reg(JITCode *code, X64Reg dst)  { emit_setcc(code, 0x9C, dst); }
void x64_setg_reg(JITCode *code, X64Reg dst)  { emit_setcc(code, 0x9F, dst); }
void x64_sete_reg(JITCode *code, X64Reg dst)  { emit_setcc(code, 0x94, dst); }
void x64_setne_reg(JITCode *code, X64Reg dst) { emit_setcc(code, 0x95, dst); }
void x64_setle_reg(JITCode *code, X64Reg dst) { emit_setcc(code, 0x9E, dst); }
void x64_setge_reg(JITCode *code, X64Reg dst) { emit_setcc(code, 0x9D, dst); }

void x64_movzx_reg_reg8(JITCode *code, X64Reg dst, X64Reg src) {
    // REX.W + 0F B6 /r (movzx r64, r/m8)
    EMIT_REX_W(dst, src);
    EMIT(0x0F);
    EMIT(0xB6);
    EMIT(MODRM_REG(dst, src));
}

// ============================================================================
// JUMP INSTRUCTIONS
// ============================================================================

size_t x64_jmp_rel32(JITCode *code) {
    // E9 cd (jmp rel32)
    EMIT(0xE9);
    size_t offset = jit_code_pos(code);
    emit_imm32(code, 0);  // Placeholder
    return offset;
}

void x64_jmp_reg(JITCode *code, X64Reg reg) {
    // FF /4 (jmp r/m64)
    if (NEEDS_REX_B(reg)) {
        EMIT(REX_BASE | REX_B);
    }
    EMIT(0xFF);
    EMIT(MODRM_REG(4, reg));
}

// Conditional jumps
static size_t emit_jcc_rel32(JITCode *code, uint8_t cc) {
    // 0F 8x cd (jcc rel32)
    EMIT(0x0F);
    EMIT(cc);
    size_t offset = jit_code_pos(code);
    emit_imm32(code, 0);
    return offset;
}

size_t x64_je_rel32(JITCode *code)  { return emit_jcc_rel32(code, 0x84); }
size_t x64_jne_rel32(JITCode *code) { return emit_jcc_rel32(code, 0x85); }
size_t x64_jl_rel32(JITCode *code)  { return emit_jcc_rel32(code, 0x8C); }
size_t x64_jle_rel32(JITCode *code) { return emit_jcc_rel32(code, 0x8E); }
size_t x64_jg_rel32(JITCode *code)  { return emit_jcc_rel32(code, 0x8F); }
size_t x64_jge_rel32(JITCode *code) { return emit_jcc_rel32(code, 0x8D); }
size_t x64_jz_rel32(JITCode *code)  { return emit_jcc_rel32(code, 0x84); }
size_t x64_jnz_rel32(JITCode *code) { return emit_jcc_rel32(code, 0x85); }

void x64_patch_jump(JITCode *code, size_t jump_offset, size_t target) {
    // Calculate relative offset (target - end of jump instruction)
    int32_t rel = (int32_t)(target - (jump_offset + 4));
    code->code[jump_offset]     = rel & 0xFF;
    code->code[jump_offset + 1] = (rel >> 8) & 0xFF;
    code->code[jump_offset + 2] = (rel >> 16) & 0xFF;
    code->code[jump_offset + 3] = (rel >> 24) & 0xFF;
}

// ============================================================================
// FUNCTION CALL INSTRUCTIONS
// ============================================================================

void x64_call_reg(JITCode *code, X64Reg reg) {
    // FF /2 (call r/m64)
    if (NEEDS_REX_B(reg)) {
        EMIT(REX_BASE | REX_B);
    }
    EMIT(0xFF);
    EMIT(MODRM_REG(2, reg));
}

size_t x64_call_rel32(JITCode *code) {
    // E8 cd (call rel32)
    EMIT(0xE8);
    size_t offset = jit_code_pos(code);
    emit_imm32(code, 0);
    return offset;
}

void x64_ret(JITCode *code) {
    // C3 (ret)
    EMIT(0xC3);
}

// ============================================================================
// STACK INSTRUCTIONS
// ============================================================================

void x64_push_reg(JITCode *code, X64Reg reg) {
    // 50+rd (push r64)
    if (NEEDS_REX_B(reg)) {
        EMIT(REX_BASE | REX_B);
    }
    EMIT(0x50 + REG_LOW(reg));
}

void x64_pop_reg(JITCode *code, X64Reg reg) {
    // 58+rd (pop r64)
    if (NEEDS_REX_B(reg)) {
        EMIT(REX_BASE | REX_B);
    }
    EMIT(0x58 + REG_LOW(reg));
}

// ============================================================================
// LOGICAL INSTRUCTIONS
// ============================================================================

void x64_and_reg_reg(JITCode *code, X64Reg dst, X64Reg src) {
    // REX.W + 21 /r (and r/m64, r64)
    EMIT_REX_W(src, dst);
    EMIT(0x21);
    EMIT(MODRM_REG(src, dst));
}

void x64_or_reg_reg(JITCode *code, X64Reg dst, X64Reg src) {
    // REX.W + 09 /r (or r/m64, r64)
    EMIT_REX_W(src, dst);
    EMIT(0x09);
    EMIT(MODRM_REG(src, dst));
}

void x64_xor_reg_reg(JITCode *code, X64Reg dst, X64Reg src) {
    // REX.W + 31 /r (xor r/m64, r64)
    EMIT_REX_W(src, dst);
    EMIT(0x31);
    EMIT(MODRM_REG(src, dst));
}

void x64_not_reg(JITCode *code, X64Reg reg) {
    // REX.W + F7 /2 (not r/m64)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_B(reg)) rex |= REX_B;
    EMIT(rex);
    EMIT(0xF7);
    EMIT(MODRM_REG(2, reg));
}

// ============================================================================
// LEA INSTRUCTION
// ============================================================================

void x64_lea_reg_mem(JITCode *code, X64Reg dst, X64Reg base, int32_t offset) {
    // REX.W + 8D /r (lea r64, m)
    uint8_t rex = REX_BASE | REX_W;
    if (NEEDS_REX_R(dst)) rex |= REX_R;
    if (NEEDS_REX_B(base)) rex |= REX_B;
    EMIT(rex);
    EMIT(0x8D);
    emit_mem_operand(code, dst, base, offset);
}

// ============================================================================
// NOP
// ============================================================================

void x64_nop(JITCode *code) {
    EMIT(0x90);
}
