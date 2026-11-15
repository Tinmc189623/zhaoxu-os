#ifndef ZHAOXU_STDLIB_WRAP_H
#define ZHAOXU_STDLIB_WRAP_H

/* newlib's stdlib.h does not declare the BSD helpers our libgloss adds. */
#include_next <stdlib.h>

char *getprogname(void);
int getdtablesize(void);

#endif
