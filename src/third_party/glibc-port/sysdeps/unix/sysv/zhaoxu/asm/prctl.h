/* arch_prctl constants for the x86_64 TLS setup.
 *
 * The kernel implements ARCH_SET_FS / ARCH_GET_FS (syscall 88) and reports
 * ENOSYS for the GS pair.  Only the numbers are shared with Linux. */
#ifndef _ZHAOXU_ASM_PRCTL_H
#define _ZHAOXU_ASM_PRCTL_H 1

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

#endif
