/*
 * Zhaoxu OS - x86_64 SysV setjmp/longjmp for the FreeType rasterizer.
 *
 * FreeType's gray rasterizer uses setjmp/longjmp for its overflow path.
 * The saved context is the first 64 bytes of the glibc jmp_buf layout, which
 * is enough for the non-local jump; the signal mask is never touched.
 */
	.text

	.globl _setjmp
	.type _setjmp, @function
_setjmp:
	movq %rbx, 0(%rdi)
	movq %rbp, 8(%rdi)
	movq %r12, 16(%rdi)
	movq %r13, 24(%rdi)
	movq %r14, 32(%rdi)
	movq %r15, 40(%rdi)
	leaq 8(%rsp), %rdx
	movq %rdx, 48(%rdi)
	movq (%rsp), %rdx
	movq %rdx, 56(%rdi)
	xorl %eax, %eax
	ret
	.size _setjmp, .-_setjmp

	.globl setjmp
	.type setjmp, @function
setjmp:
	jmp _setjmp
	.size setjmp, .-setjmp

	.globl longjmp
	.type longjmp, @function
longjmp:
	movq 0(%rdi), %rbx
	movq 8(%rdi), %rbp
	movq 16(%rdi), %r12
	movq 24(%rdi), %r13
	movq 32(%rdi), %r14
	movq 40(%rdi), %r15
	movq 48(%rdi), %rsp
	movq 56(%rdi), %rdx
	movl %esi, %eax
	testl %eax, %eax
	jnz 1f
	movl $1, %eax
1:
	jmp *%rdx
	.size longjmp, .-longjmp

	.globl __longjmp_chk
	.type __longjmp_chk, @function
__longjmp_chk:
	jmp longjmp
	.size __longjmp_chk, .-__longjmp_chk
