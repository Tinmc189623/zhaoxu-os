#ifndef ZHAOXU_RANDOM_H
#define ZHAOXU_RANDOM_H

#include <types.h>

/* Fill `buffer` with cryptographically unpredictable bytes. */
void random_bytes(void *buffer, usize length);

#endif
