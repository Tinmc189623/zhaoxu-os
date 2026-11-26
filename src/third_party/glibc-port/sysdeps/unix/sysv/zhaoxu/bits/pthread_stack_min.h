/* Minimum thread stack size, in bytes.  The Zhaoxu kernel maps a full page
 * granule anyway; 16 KiB matches the generic glibc value. */
#ifndef _ZHAOXU_PTHREAD_STACK_MIN_H
#define _ZHAOXU_PTHREAD_STACK_MIN_H 1

#define PTHREAD_STACK_MIN 16384

#endif
