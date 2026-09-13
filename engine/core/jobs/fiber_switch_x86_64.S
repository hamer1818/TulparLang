// x86_64 SysV fiber gecisi. Callee-saved: rbx rbp r12-r15 (+ MXCSR/x87 CW
// dokunulmadigi icin saklanmiyor).
//   void tulpar_fiber_switch(void **save_sp /*rdi*/, void *load_sp /*rsi*/);
// Yeni fiber yigini fiber_prepare() tarafindan pop sirasiyla uyumlu dizilir:
//   sp -> r15 r14 r13(arg) r12(fn) rbx rbp [ret = trampoline]
#if defined(__x86_64__)
#if defined(__APPLE__)
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
    .text
    .globl SYM(tulpar_fiber_switch)
#if defined(__ELF__)
    .type SYM(tulpar_fiber_switch), @function
#endif
    .p2align 4
SYM(tulpar_fiber_switch):
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
#if defined(__ELF__)
    .size SYM(tulpar_fiber_switch), .-SYM(tulpar_fiber_switch)
#endif

    .globl SYM(tulpar_fiber_trampoline)
#if defined(__ELF__)
    .type SYM(tulpar_fiber_trampoline), @function
#endif
    .p2align 4
SYM(tulpar_fiber_trampoline):
    andq $-16, %rsp          // cagri oncesi 16 hizali
    movq %r13, %rdi          // arg
    callq *%r12              // fn(arg) — ASLA donmez
    ud2
#if defined(__ELF__)
    .size SYM(tulpar_fiber_trampoline), .-SYM(tulpar_fiber_trampoline)
    .section .note.GNU-stack,"",@progbits
#endif
#endif
