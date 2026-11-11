#ifndef ZHAOXU_KPRINTF_H
#define ZHAOXU_KPRINTF_H

#include <types.h>
#include <stdarg.h>

/* Host-side stand-in for src/kernel/include/kprintf.h.  The ext4 core only
 * formats its log lines through kvsnprintf; the host harness (Task 2)
 * provides the definition. */

int kvsnprintf(char *out, usize capacity, const char *fmt, va_list ap);
int ksnprintf(char *out, usize capacity, const char *fmt, ...);
int kprintf(const char *fmt, ...);

#endif
