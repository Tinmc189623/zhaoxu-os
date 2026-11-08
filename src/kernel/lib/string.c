#include <string.h>

void *memset(void *dst, int value, size_t n)
{
    u8 *d = (u8 *)dst;
    while (n--)
        *d++ = (u8)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *x = (const u8 *)a;
    const u8 *y = (const u8 *)b;
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (int)(u8)*a - (int)(u8)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *result = dst;
    while ((*dst++ = *src++))
        ;
    return result;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *result = dst;
    while (n && *src) {
        *dst++ = *src++;
        n--;
    }
    while (n--) {
        *dst++ = '\0';
    }
    return result;
}

char *strchr(const char *s, int c)
{
    char target = (char)c;
    while (*s) {
        if (*s == target)
            return (char *)s;
        s++;
    }
    return target == '\0' ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    char target = (char)c;
    while (*s) {
        if (*s == target)
            last = s;
        s++;
    }
    if (target == '\0')
        return (char *)s;
    return (char *)last;
}

int atoi(const char *s)
{
    int sign = 1;
    int value = 0;
    while (isspace((int)(u8)*s))
        s++;
    if (*s == '-' || *s == '+') {
        if (*s == '-')
            sign = -1;
        s++;
    }
    while (isdigit((int)(u8)*s)) {
        value = value * 10 + (*s - '0');
        s++;
    }
    return sign * value;
}

int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int toupper(int c)
{
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    return c;
}

int tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}
