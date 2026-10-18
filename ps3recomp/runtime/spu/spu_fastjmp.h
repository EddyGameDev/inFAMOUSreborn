/* spu_fastjmp.h -- v157: non-unwinding setjmp/longjmp for the SPU halt pad and SPU irq frames.
 *
 * Why: under clang-cl (MSVC x64 ABI) the CRT longjmp is a "safe" one -- RtlUnwindEx walks every host
 * frame between the longjmp and its setjmp. Every SPURS policy-module run ends in spu_halt -> longjmp
 * (play155/156 [LANEPROF] halt: w2 2.9%, w3 5.8%, w4 9.6% of lane time; ~36% of a w2 idle run).
 * Nothing between those frames needs unwinding (C runtime + lifted C; no __try/__finally, no C++
 * objects), so a plain register restore is enough.
 *
 * v156 tried clang's __builtin_setjmp/__builtin_longjmp for this and crashed at boot (0xC0000005,
 * bisect156 T2). Root cause (clang 18/19, x86_64-pc-windows-msvc): __builtin_setjmp stores
 * llvm.frameaddress(0) into buf[0], which on Win64 is the CANONICAL frame address (rbp minus the
 * .seh_setframe offset, e.g. rbp-128), while __builtin_longjmp loads buf[0] straight back into rbp.
 * After the jump rbp is off by that offset and every rbp-relative spill reload in the landing code
 * reads the wrong slot. (On Linux SysV frameaddress == rbp, which is why the v156 unit test passed.)
 *
 * This file's pair saves/restores EXACTLY what the Win64 ABI calls non-volatile: rbx rbp rdi rsi
 * r12-r15, rsp, xmm6-xmm15, MXCSR and the x87 control word -- the same set the CRT longjmp restores,
 * minus the SEH unwind. Contract is the C setjmp contract (non-volatile locals modified after the set
 * are indeterminate), same as the CRT pair it replaces. spu_fj_jump(b, 0) returns 1, like longjmp.
 * Not usable with a user-mode CET shadow stack (callers check; the exe is not /CETCOMPAT).
 *
 * Exactly one TU defines the functions: #define SPU_FASTJMP_DEFINE before including (spu_channels.c).
 * SPU_FASTJMP_TEST_MSABI: Linux unit test of the same Win64 asm through __attribute__((ms_abi)). */
#ifndef SPU_FASTJMP_H
#define SPU_FASTJMP_H
#include <stdint.h>

typedef struct spu_fjbuf { uint64_t q[32]; } spu_fjbuf;   /* 256 bytes (= MSVC x64 jmp_buf size) */

#if defined(__clang__) && defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
#  define SPU_HAVE_FASTJMP 1
#  define SPU_FJ_ABI
#elif defined(__clang__) && defined(__x86_64__) && defined(SPU_FASTJMP_TEST_MSABI)
#  define SPU_HAVE_FASTJMP 1
#  define SPU_FJ_ABI __attribute__((ms_abi))
#else
#  define SPU_HAVE_FASTJMP 0
#endif

#if SPU_HAVE_FASTJMP
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((returns_twice)) SPU_FJ_ABI int  spu_fj_set(spu_fjbuf* b);
__attribute__((noreturn))      SPU_FJ_ABI void spu_fj_jump(spu_fjbuf* b, int v);
#ifdef __cplusplus
}
#endif

#ifdef SPU_FASTJMP_DEFINE
#if defined(__AVX__)
#  define SPU_FJ_MOVX "vmovups"
#else
#  define SPU_FJ_MOVX "movups"
#endif
/* Layout: 0 rbx, 8 rbp, 16 rdi, 24 rsi, 32 r12, 40 r13, 48 r14, 56 r15, 64 rsp-after-return,
 * 72 return rip, 80 mxcsr (4), 84 x87 cw (2), 96..255 xmm6..xmm15. */
__attribute__((naked)) SPU_FJ_ABI int spu_fj_set(spu_fjbuf* b)
{
    __asm__ volatile(
        "movq %rbx,   0(%rcx)\n\t"
        "movq %rbp,   8(%rcx)\n\t"
        "movq %rdi,  16(%rcx)\n\t"
        "movq %rsi,  24(%rcx)\n\t"
        "movq %r12,  32(%rcx)\n\t"
        "movq %r13,  40(%rcx)\n\t"
        "movq %r14,  48(%rcx)\n\t"
        "movq %r15,  56(%rcx)\n\t"
        "leaq 8(%rsp), %rdx\n\t"
        "movq %rdx,  64(%rcx)\n\t"
        "movq (%rsp), %rdx\n\t"
        "movq %rdx,  72(%rcx)\n\t"
        "stmxcsr     80(%rcx)\n\t"
        "fnstcw      84(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm6,   96(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm7,  112(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm8,  128(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm9,  144(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm10, 160(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm11, 176(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm12, 192(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm13, 208(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm14, 224(%rcx)\n\t"
        SPU_FJ_MOVX " %xmm15, 240(%rcx)\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t");
}

__attribute__((naked, noreturn)) SPU_FJ_ABI void spu_fj_jump(spu_fjbuf* b, int v)
{
    __asm__ volatile(
        "movl %edx, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n"
        "1:\n\t"
        "movq   0(%rcx), %rbx\n\t"
        "movq   8(%rcx), %rbp\n\t"
        "movq  16(%rcx), %rdi\n\t"
        "movq  24(%rcx), %rsi\n\t"
        "movq  32(%rcx), %r12\n\t"
        "movq  40(%rcx), %r13\n\t"
        "movq  48(%rcx), %r14\n\t"
        "movq  56(%rcx), %r15\n\t"
        "ldmxcsr 80(%rcx)\n\t"
        "fldcw   84(%rcx)\n\t"
        SPU_FJ_MOVX "  96(%rcx), %xmm6\n\t"
        SPU_FJ_MOVX " 112(%rcx), %xmm7\n\t"
        SPU_FJ_MOVX " 128(%rcx), %xmm8\n\t"
        SPU_FJ_MOVX " 144(%rcx), %xmm9\n\t"
        SPU_FJ_MOVX " 160(%rcx), %xmm10\n\t"
        SPU_FJ_MOVX " 176(%rcx), %xmm11\n\t"
        SPU_FJ_MOVX " 192(%rcx), %xmm12\n\t"
        SPU_FJ_MOVX " 208(%rcx), %xmm13\n\t"
        SPU_FJ_MOVX " 224(%rcx), %xmm14\n\t"
        SPU_FJ_MOVX " 240(%rcx), %xmm15\n\t"
        "movq  64(%rcx), %rsp\n\t"
        "jmpq *72(%rcx)\n\t");
}
#undef SPU_FJ_MOVX
#endif /* SPU_FASTJMP_DEFINE */
#endif /* SPU_HAVE_FASTJMP */
#endif /* SPU_FASTJMP_H */
