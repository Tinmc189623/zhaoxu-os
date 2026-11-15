#ifndef ZHAOXU_FREETYPE_STDLIB_H
#define ZHAOXU_FREETYPE_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void free(void *);
void *realloc(void *, size_t);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
#endif
