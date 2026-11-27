/* Zhaoxu OS syscall and assembler macros for glibc.
 *
 * The kernel gate is `int $0x80` with rax = number and the Linux x86_64
 * argument registers (rdi, rsi, rdx, r10, r8, r9).  Calls 86 and up return
 * -errno directly; calls 0-85 keep the legacy -1 convention and expose the
 * errno through SYS_LAST_ERROR (112), so both stub paths are normalised to
 * the -errno form glibc expects.
 */
#ifndef _ZHAOXU_SYSDEP_H
#define _ZHAOXU_SYSDEP_H 1

#include <sysdeps/unix/sysdep.h>
#include <sysdeps/x86_64/sysdep.h>

#ifdef __ASSEMBLER__

# undef DO_CALL
# define DO_CALL(syscall_name, args)			\
    DOARGS_##args					\
    movl $SYS_ify (syscall_name), %eax;			\
    int $0x80;

# define DOARGS_0 /* nothing */
# define DOARGS_1 /* nothing */
# define DOARGS_2 /* nothing */
# define DOARGS_3 /* nothing */
# define DOARGS_4 movq %rcx, %r10;
# define DOARGS_5 DOARGS_4
# define DOARGS_6 DOARGS_5

/* Legacy calls (0-85) report failure as exactly -1; convert that to -errno
 * before the common range check.  The number is kept in %r11d and compared
 * at run time: cpp macro expansion cannot emit the newline an `.if` needs,
 * and %r11 is already clobbered by the syscall ABI. */
# undef PSEUDO
# define PSEUDO(name, syscall_name, args)				\
  .text;								\
  ENTRY (name)								\
    movl $SYS_ify (syscall_name), %r11d;				\
    movl %r11d, %eax;							\
    DOARGS_##args							\
    int $0x80;								\
    cmpl $85, %r11d;							\
    ja 2f;								\
    cmpq $-1, %rax;							\
    jne 2f;								\
    movl $112, %eax;							\
    int $0x80;								\
    testl %eax, %eax;							\
    jne 1f;								\
    movl $5, %eax;		/* EIO when nothing was recorded */	\
1:  negl %eax;								\
2:  cmpq $-4095, %rax;							\
    jae SYSCALL_ERROR_LABEL

# undef PSEUDO_END
# define PSEUDO_END(name)			\
  SYSCALL_ERROR_HANDLER				\
  END (name)

# undef PSEUDO_NOERRNO
# define PSEUDO_NOERRNO(name, syscall_name, args)	\
  .text;						\
  ENTRY (name)						\
    DO_CALL (syscall_name, args)

# undef PSEUDO_END_NOERRNO
# define PSEUDO_END_NOERRNO(name) \
  END (name)

# define ret_NOERRNO ret

# undef PSEUDO_ERRVAL
# define PSEUDO_ERRVAL(name, syscall_name, args)	\
  .text;						\
  ENTRY (name)						\
    DO_CALL (syscall_name, args)			\
    negq %rax

# undef PSEUDO_END_ERRVAL
# define PSEUDO_END_ERRVAL(name) \
  END (name)

# define ret_ERRVAL ret

# if IS_IN (libc)
#  define SYSCALL_ERROR_ERRNO __libc_errno
# else
#  define SYSCALL_ERROR_ERRNO errno
# endif
# define SYSCALL_SET_ERRNO			\
  movq SYSCALL_ERROR_ERRNO@GOTTPOFF(%rip), %rcx;	\
  neg %eax;					\
  movl %eax, %fs:(%rcx);

# define SYSCALL_ERROR_HANDLER			\
0:						\
  SYSCALL_SET_ERRNO;				\
  or $-1, %RAX_LP;				\
  ret;

# define SYSCALL_ERROR_LABEL 0f

#else	/* !__ASSEMBLER__ */

/* The generic Unix layer provides INTERNAL_SYSCALL_CALL dispatch and the
 * cancellation macros; the Linux-wide helper header supplies the INLINE_*
 * error wrappers, which are OS-neutral apart from their name.  Everything
 * the machine layer has to provide is the internal_syscallN family below. */
#include <errno.h>
#include <bits/wordsize.h>
#include <kernel-features.h>
#include <sysdeps/unix/sysv/linux/sysdep.h>

/* int 0x80 preserves every general register; only flags change. */
# define REGISTERS_CLOBBERED_BY_SYSCALL "cc"

#undef INTERNAL_SYSCALL
#define INTERNAL_SYSCALL(name, nr, args...) \
	internal_syscall##nr (SYS_ify (name), args)

#undef INTERNAL_SYSCALL_NCS
#define INTERNAL_SYSCALL_NCS(number, nr, args...) \
	internal_syscall##nr (number, args)

/* Legacy calls (0-85) report -1 and keep the errno in SYS_LAST_ERROR. */
static __always_inline long int
__zhaoxu_legacy_result (long int number, long int result)
{
  unsigned long int error;
  if (number > 85 || result != -1)
    return result;
  asm volatile ("int $0x80"
		: "=a" (error)
		: "a" (112L)
		: "memory", "cc");
  return error ? -(long int) error : -5;
}

#undef internal_syscall0
#define internal_syscall0(number, dummy...)				\
({									\
    unsigned long int resultvar;					\
    asm volatile ("int $0x80"						\
    : "=a" (resultvar)							\
    : "0" ((unsigned long int) (number))				\
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);			\
    __zhaoxu_legacy_result ((long int) (number), (long int) resultvar);	\
})

#undef internal_syscall1
#define internal_syscall1(number, arg1)					\
({									\
    unsigned long int resultvar;					\
    register long int _a1 asm ("rdi") = (long int) (arg1);		\
    asm volatile ("int $0x80"						\
    : "=a" (resultvar)							\
    : "0" ((unsigned long int) (number)), "r" (_a1)			\
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);			\
    __zhaoxu_legacy_result ((long int) (number), (long int) resultvar);	\
})

#undef internal_syscall2
#define internal_syscall2(number, arg1, arg2)				\
({									\
    unsigned long int resultvar;					\
    register long int _a1 asm ("rdi") = (long int) (arg1);		\
    register long int _a2 asm ("rsi") = (long int) (arg2);		\
    asm volatile ("int $0x80"						\
    : "=a" (resultvar)							\
    : "0" ((unsigned long int) (number)), "r" (_a1), "r" (_a2)		\
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);			\
    __zhaoxu_legacy_result ((long int) (number), (long int) resultvar);	\
})

#undef internal_syscall3
#define internal_syscall3(number, arg1, arg2, arg3)			\
({									\
    unsigned long int resultvar;					\
    register long int _a1 asm ("rdi") = (long int) (arg1);		\
    register long int _a2 asm ("rsi") = (long int) (arg2);		\
    register long int _a3 asm ("rdx") = (long int) (arg3);		\
    asm volatile ("int $0x80"						\
    : "=a" (resultvar)							\
    : "0" ((unsigned long int) (number)), "r" (_a1), "r" (_a2),		\
      "r" (_a3)								\
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);			\
    __zhaoxu_legacy_result ((long int) (number), (long int) resultvar);	\
})

#undef internal_syscall4
#define internal_syscall4(number, arg1, arg2, arg3, arg4)		\
({									\
    unsigned long int resultvar;					\
    register long int _a1 asm ("rdi") = (long int) (arg1);		\
    register long int _a2 asm ("rsi") = (long int) (arg2);		\
    register long int _a3 asm ("rdx") = (long int) (arg3);		\
    register long int _a4 asm ("r10") = (long int) (arg4);		\
    asm volatile ("int $0x80"						\
    : "=a" (resultvar)							\
    : "0" ((unsigned long int) (number)), "r" (_a1), "r" (_a2),		\
      "r" (_a3), "r" (_a4)						\
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);			\
    __zhaoxu_legacy_result ((long int) (number), (long int) resultvar);	\
})

#undef internal_syscall5
#define internal_syscall5(number, arg1, arg2, arg3, arg4, arg5)		\
({									\
    unsigned long int resultvar;					\
    register long int _a1 asm ("rdi") = (long int) (arg1);		\
    register long int _a2 asm ("rsi") = (long int) (arg2);		\
    register long int _a3 asm ("rdx") = (long int) (arg3);		\
    register long int _a4 asm ("r10") = (long int) (arg4);		\
    register long int _a5 asm ("r8") = (long int) (arg5);		\
    asm volatile ("int $0x80"						\
    : "=a" (resultvar)							\
    : "0" ((unsigned long int) (number)), "r" (_a1), "r" (_a2),		\
      "r" (_a3), "r" (_a4), "r" (_a5)					\
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);			\
    __zhaoxu_legacy_result ((long int) (number), (long int) resultvar);	\
})

#undef internal_syscall6
#define internal_syscall6(number, arg1, arg2, arg3, arg4, arg5, arg6)	\
({									\
    unsigned long int resultvar;					\
    register long int _a1 asm ("rdi") = (long int) (arg1);		\
    register long int _a2 asm ("rsi") = (long int) (arg2);		\
    register long int _a3 asm ("rdx") = (long int) (arg3);		\
    register long int _a4 asm ("r10") = (long int) (arg4);		\
    register long int _a5 asm ("r8") = (long int) (arg5);		\
    register long int _a6 asm ("r9") = (long int) (arg6);		\
    asm volatile ("int $0x80"						\
    : "=a" (resultvar)							\
    : "0" ((unsigned long int) (number)), "r" (_a1), "r" (_a2),		\
      "r" (_a3), "r" (_a4), "r" (_a5), "r" (_a6)			\
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);			\
    __zhaoxu_legacy_result ((long int) (number), (long int) resultvar);	\
})

# define VDSO_NAME  "LINUX_2.6"
# define VDSO_HASH  61765110

#endif	/* __ASSEMBLER__ */

#endif /* _ZHAOXU_SYSDEP_H */
