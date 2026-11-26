/* POSIX semaphore storage for the port.
 *
 * 32 bytes on x86_64, the same size the NPTL futex-based implementation
 * assumes; the kernel only needs the futex word inside it. */
#ifndef _ZHAOXU_BITS_SEMAPHORE_H
#define _ZHAOXU_BITS_SEMAPHORE_H 1

#ifndef _SEMAPHORE_H
# error "Never use <bits/semaphore.h> directly; include <semaphore.h> instead."
#endif

#include <bits/wordsize.h>

#if __WORDSIZE == 64
# define __SIZEOF_SEM_T 32
#else
# define __SIZEOF_SEM_T 16
#endif

#define SEM_FAILED ((sem_t *) 0)

typedef union
{
  char __size[__SIZEOF_SEM_T];
  long int __align;
} sem_t;

#endif
