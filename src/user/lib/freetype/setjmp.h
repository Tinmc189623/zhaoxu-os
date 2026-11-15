#ifndef ZHAOXU_FREETYPE_SETJMP_H
#define ZHAOXU_FREETYPE_SETJMP_H
typedef unsigned long jmp_buf[8];
int setjmp(jmp_buf);
void longjmp(jmp_buf, int);
#endif
