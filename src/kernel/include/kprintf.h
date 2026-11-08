#ifndef ZHAOXU_KPRINTF_H
#define ZHAOXU_KPRINTF_H

#include <types.h>
#include <stdarg.h>

int kvsnprintf(char *out, usize capacity, const char *fmt, va_list ap);
int ksnprintf(char *out, usize capacity, const char *fmt, ...);
int kprintf(const char *fmt, ...);

#endif
