/* String helpers not provided by user/userlib.c (which owns mem*). */
#include <string.h>
#include <types.h>

size_t strlen(const char *text)
{
    size_t length = 0;
    while (text[length])
        length++;
    return length;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, size_t count)
{
    while (count && *a && *a == *b) {
        a++;
        b++;
        count--;
    }
    if (count == 0)
        return 0;
    return (int)(u8)*a - (int)(u8)*b;
}

char *strncpy(char *destination, const char *source, size_t count)
{
    size_t i = 0;
    for (; i < count && source[i]; i++)
        destination[i] = source[i];
    for (; i < count; i++)
        destination[i] = 0;
    return destination;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;
    while ((*destination++ = *source++))
        ;
    return result;
}

char *strchr(const char *text, int character)
{
    while (*text) {
        if (*text == (char)character)
            return (char *)text;
        text++;
    }
    return character == 0 ? (char *)text : 0;
}

int memcmp(const void *a, const void *b, size_t count)
{
    const u8 *x = (const u8 *)a;
    const u8 *y = (const u8 *)b;
    while (count--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}
