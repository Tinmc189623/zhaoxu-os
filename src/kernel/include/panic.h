#ifndef ZHAOXU_PANIC_H
#define ZHAOXU_PANIC_H

#include <types.h>

/* Red fatal-error screen; prints the report and halts the machine. */
void panic_red_screen(u32 vector, const char *name, u64 error, u64 rip,
                      u64 cr2, int page_fault) __attribute__((noreturn));

#endif
