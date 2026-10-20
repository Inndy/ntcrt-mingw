/*
 * setjmp and longjmp of x86, which the ntdll of x86 does not export (that of x64 does, see ntdll_crt.txt). The
 * headers of mingw-w64 make setjmp _setjmp3, the jmp_buf is the _JUMP_BUFFER of msvcrt: the registers, the SEH
 * registration of the thread (fs:0) and the scope of _except_handler3 (none, -1). longjmp unwinds the SEH frames
 * registered since by RtlUnwind, as msvcrt does, then restores the registers. Defined with "__imp_" here, not by the
 * stubs of the Makefile (NTCRT_IMPORT), which would replace those of ntdll on x64.
 */

#include "NtCRT.h"

#ifdef __i386__

/* int _setjmp3(jmp_buf Buffer, int Count, ...) and the older int _setjmp(jmp_buf Buffer) */
__asm__(".text\n\t"
        ".globl __setjmp3\n\t"
        ".globl __setjmp\n\t"
        ".def __setjmp3; .scl 2; .type 32; .endef\n\t"
        ".def __setjmp; .scl 2; .type 32; .endef\n"
        "__setjmp3:\n"
        "__setjmp:\n\t"
        "movl 4(%esp), %edx\n\t"
        "movl %ebp, 0(%edx)\n\t"
        "movl %ebx, 4(%edx)\n\t"
        "movl %edi, 8(%edx)\n\t"
        "movl %esi, 12(%edx)\n\t"
        "leal 4(%esp), %eax\n\t"
        "movl %eax, 16(%edx)\n\t"
        "movl (%esp), %eax\n\t"
        "movl %eax, 20(%edx)\n\t"
        "movl %fs:0, %eax\n\t"
        "movl %eax, 24(%edx)\n\t"
        "movl $-1, 28(%edx)\n\t"
        "movl $0x56433230, 32(%edx)\n\t" /* "VC20", the cookie of _setjmp3 */
        "movl $0, 36(%edx)\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t"
        ".section .rdata$__imp___setjmp3,\"dr\"\n\t"
        ".linkonce discard\n\t"
        ".globl __imp___setjmp3\n\t"
        ".p2align 2\n"
        "__imp___setjmp3:\n\t"
        ".long __setjmp3\n\t"
        ".section .rdata$__imp___setjmp,\"dr\"\n\t"
        ".linkonce discard\n\t"
        ".globl __imp___setjmp\n\t"
        ".p2align 2\n"
        "__imp___setjmp:\n\t"
        ".long __setjmp\n\t"
        ".text");

/*
 * void longjmp(jmp_buf Buffer, int Value): Value, or 1 for 0, is the return of setjmp. RtlUnwind(Frame, TargetIp,
 * NULL, 0) of x86 may not keep ebx, esi and edi, they are kept on the stack as _global_unwind2 of msvcrt does.
 */
__asm__(".text\n\t"
        ".globl _longjmp\n\t"
        ".def _longjmp; .scl 2; .type 32; .endef\n"
        "_longjmp:\n\t"
        "movl 4(%esp), %ebx\n\t"
        "movl 8(%esp), %esi\n\t"
        "movl 24(%ebx), %eax\n\t"
        "cmpl %fs:0, %eax\n\t"
        "je 1f\n\t"
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "pushl $0\n\t"
        "pushl $0\n\t"
        "pushl $3f\n\t"
        "pushl %eax\n\t"
        "call _RtlUnwind@16\n"
        "3:\n\t"
        "popl %esi\n\t"
        "popl %ebx\n"
        "1:\n\t"
        "movl %esi, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 2f\n\t"
        "incl %eax\n"
        "2:\n\t"
        "movl 0(%ebx), %ebp\n\t"
        "movl 8(%ebx), %edi\n\t"
        "movl 12(%ebx), %esi\n\t"
        "movl 16(%ebx), %esp\n\t"
        "movl 20(%ebx), %ecx\n\t"
        "movl 4(%ebx), %ebx\n\t"
        "jmp *%ecx\n\t"
        ".section .rdata$__imp__longjmp,\"dr\"\n\t"
        ".linkonce discard\n\t"
        ".globl __imp__longjmp\n\t"
        ".p2align 2\n"
        "__imp__longjmp:\n\t"
        ".long _longjmp\n\t"
        ".text");

#endif
