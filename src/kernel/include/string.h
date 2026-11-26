#ifndef ZHAOXU_STRING_H
#define ZHAOXU_STRING_H

#include <stddef.h>
#include <types.h>

/* The FreeBSD personality in OS_5P defines the same C string family, so the
 * core keeps its copies under a yuanshuo_ prefix; these aliases keep call
 * sites unchanged while the symbol stays separate. */
void *yuanshuo_memset(void *dst, int value, size_t n);
void *yuanshuo_memcpy(void *dst, const void *src, size_t n);
void *yuanshuo_memmove(void *dst, const void *src, size_t n);
void *yuanshuo_memchr(const void *s, int c, size_t n);
int yuanshuo_memcmp(const void *a, const void *b, size_t n);
size_t yuanshuo_strlen(const char *s);
int yuanshuo_strcmp(const char *a, const char *b);
int yuanshuo_strncmp(const char *a, const char *b, size_t n);
char *yuanshuo_strcpy(char *dst, const char *src);
char *yuanshuo_strncpy(char *dst, const char *src, size_t n);
char *yuanshuo_strchr(const char *s, int c);
char *yuanshuo_strrchr(const char *s, int c);
char *yuanshuo_strstr(const char *haystack, const char *needle);
char *yuanshuo_strcat(char *dst, const char *src);

#ifdef ZHAOXU_KERNEL_BUILD
#define memset yuanshuo_memset
#define memcpy yuanshuo_memcpy
#define memmove yuanshuo_memmove
#define memchr yuanshuo_memchr
#define memcmp yuanshuo_memcmp
#define strlen yuanshuo_strlen
#define strcmp yuanshuo_strcmp
#define strncmp yuanshuo_strncmp
#define strcpy yuanshuo_strcpy
#define strncpy yuanshuo_strncpy
#define strchr yuanshuo_strchr
#define strrchr yuanshuo_strrchr
#define strstr yuanshuo_strstr
#define strcat yuanshuo_strcat
#else
/* Shared with the Ring3 side (kernel/gfx, user/lib), which keeps the plain
 * names and links them against its own copies. */
void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strcat(char *dst, const char *src);
#endif

int atoi(const char *s);
int isspace(int c);
int isdigit(int c);
int toupper(int c);
int tolower(int c);

#endif
