#ifndef ZHAOXU_TYPES_H
#define ZHAOXU_TYPES_H

typedef unsigned char       u8;
typedef signed char         i8;
typedef unsigned short      u16;
typedef signed short        i16;
typedef unsigned int        u32;
typedef signed int          i32;
typedef unsigned long long  u64;
typedef signed long long    i64;
typedef unsigned long       usize;
typedef signed long         isize;
typedef unsigned long       uintptr_t;

#define NULL ((void *)0)
#define TRUE  1
#define FALSE 0

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

#endif
