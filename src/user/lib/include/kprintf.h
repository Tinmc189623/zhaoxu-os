#ifndef ZHAOXU_USER_KPRINTF_H
#define ZHAOXU_USER_KPRINTF_H

#include <types.h>

void kprintf(const char *format, ...);
int ksnprintf(char *buffer, usize capacity, const char *format, ...);

#endif
