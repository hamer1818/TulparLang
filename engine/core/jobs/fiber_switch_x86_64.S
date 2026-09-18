// x86_64 fiber gecisi — IKI ABI.
//
// SysV (Linux/macOS/Android): argumanlar rdi/rsi, callee-saved rbx rbp r12-r15.
// Win64 (mingw): argumanlar rcx/rdx ve callee-saved kume DAHA GENIS:
//   rbx rbp rdi rsi r12-r15  +  XMM6-XMM15 (10 x 16 bayt)
// Ustelik Windows'ta YIGIN TEB'de KAYITLIDIR: StackBase (gs:0x08),
// StackLimit (gs:0x10) ve DeallocationStack (gs:0x1478). Fiber'a gecerken
// bunlar guncellenmezse yigin tasma denetimi ve SEH cozumu BASKA bir yigini
// dogru sanar — cokmeler "imkansiz" yerlerde cikar. Bu yuzden Win64 dalinda
// uc alan da kaydedilip geri yukleniyor.
//
//   void tulpar_fiber_switch(void **save_sp, void *load_sp);
// Yeni fiber yigini fiber_prepare() tarafindan pop sirasiyla uyumlu dizilir
// (iki ABI icin ayri duzen; bkz. fiber.cpp).
#if defined(__x86_64__)
#if defined(__APPLE__)
#define SYM(x) _##x
#else
#define SYM(x) x
#endif

#if defined(_WIN64)
// ---------------------------------------------------------------- Win64 ----
    .text
    .globl SYM(tulpar_fiber_switch)
    .def SYM(tulpar_fiber_switch); .scl 2; .type 32; .endef
    .p2align 4
SYM(tulpar_fiber_switch):
    pushq %rbp
    pushq %rbx
    pushq %rdi
    pushq %rsi
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    // 160 bayt XMM + 8 bayt dolgu: giriste rsp%16==8 oldugu ve 8 push
    // hizalamayi degistirmedigi icin 168 cikarmak rsp'yi 16'ya HIZALAR
    // (movaps hizasiz adreste cokerdi — Tuzaklar 8ap'nin ayni sinifi).
    subq $168, %rsp
    movaps %xmm6,    0(%rsp)
    movaps %xmm7,   16(%rsp)
    movaps %xmm8,   32(%rsp)
    movaps %xmm9,   48(%rsp)
    movaps %xmm10,  64(%rsp)
    movaps %xmm11,  80(%rsp)
    movaps %xmm12,  96(%rsp)
    movaps %xmm13, 112(%rsp)
    movaps %xmm14, 128(%rsp)
    movaps %xmm15, 144(%rsp)
    pushq %gs:0x08          // StackBase
    pushq %gs:0x10          // StackLimit
    pushq %gs:0x1478        // DeallocationStack
    movq %rsp, (%rcx)
    movq %rdx, %rsp
    popq %gs:0x1478
    popq %gs:0x10
    popq %gs:0x08
    movaps    0(%rsp), %xmm6
    movaps   16(%rsp), %xmm7
    movaps   32(%rsp), %xmm8
    movaps   48(%rsp), %xmm9
    movaps   64(%rsp), %xmm10
    movaps   80(%rsp), %xmm11
    movaps   96(%rsp), %xmm12
    movaps  112(%rsp), %xmm13
    movaps  128(%rsp), %xmm14
    movaps  144(%rsp), %xmm15
    addq $168, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rsi
    popq %rdi
    popq %rbx
    popq %rbp
    ret

    .globl SYM(tulpar_fiber_trampoline)
    .def SYM(tulpar_fiber_trampoline); .scl 2; .type 32; .endef
    .p2align 4
SYM(tulpar_fiber_trampoline):
    andq $-16, %rsp          // cagri oncesi 16 hizali
    movq %r13, %rcx          // arg — Win64'te 1. arguman RCX
    subq $32, %rsp           // GOLGE ALAN (shadow space): Win64'te ZORUNLU
    callq *%r12              // fn(arg) — ASLA donmez
    ud2

#else
// ----------------------------------------------------------------- SysV ----
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
#endif  // _WIN64 / SysV
#endif  // __x86_64__
