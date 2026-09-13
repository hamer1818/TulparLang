// AArch64 AAPCS64 fiber gecisi. Callee-saved: x19-x28, x29(fp), x30(lr),
// d8-d15 (alt 64 bit). sp her an 16 hizali.
//   void tulpar_fiber_switch(void **save_sp /*x0*/, void *load_sp /*x1*/);
// Yeni fiber yigini (fiber_prepare): [sp+0]=x19(fn) [sp+8]=x20(arg)
// [sp+88]=x30(trampoline), digerleri 0; cerceve 160 bayt.
#if defined(__aarch64__)
#if defined(__APPLE__)
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
    .text
    .globl SYM(tulpar_fiber_switch)
#if defined(__ELF__)
    .type SYM(tulpar_fiber_switch), %function
#endif
    .p2align 2
SYM(tulpar_fiber_switch):
    sub sp, sp, #160
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp d8,  d9,  [sp, #96]
    stp d10, d11, [sp, #112]
    stp d12, d13, [sp, #128]
    stp d14, d15, [sp, #144]
    mov x9, sp
    str x9, [x0]
    mov sp, x1
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    ldp d8,  d9,  [sp, #96]
    ldp d10, d11, [sp, #112]
    ldp d12, d13, [sp, #128]
    ldp d14, d15, [sp, #144]
    add sp, sp, #160
    ret
#if defined(__ELF__)
    .size SYM(tulpar_fiber_switch), .-SYM(tulpar_fiber_switch)
#endif

    .globl SYM(tulpar_fiber_trampoline)
#if defined(__ELF__)
    .type SYM(tulpar_fiber_trampoline), %function
#endif
    .p2align 2
SYM(tulpar_fiber_trampoline):
    mov x0, x20              // arg
    blr x19                  // fn(arg) — ASLA donmez
    brk #1
#if defined(__ELF__)
    .size SYM(tulpar_fiber_trampoline), .-SYM(tulpar_fiber_trampoline)
    .section .note.GNU-stack,"",%progbits
#endif
#endif
