/*
 * Zhaoxu OS - extra libc entry points required by the FreeType port.
 *
 * The libwayland compatibility layer already supplies allocation, strings,
 * errno and formatting.  FreeType additionally needs qsort, memchr, strcat,
 * strncpy and strrchr; keeping them here avoids pulling the Wayland socket
 * surface into the font engine.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void swap_bytes(unsigned char *a, unsigned char *b, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        unsigned char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *))
{
    unsigned char *bytes = base;
    for (size_t i = 1; i < count; i++) {
        size_t j = i;
        while (j > 0 &&
               compare(bytes + (j - 1) * size, bytes + j * size) > 0) {
            swap_bytes(bytes + (j - 1) * size, bytes + j * size, size);
            j--;
        }
    }
}

#undef strchr
__attribute__((weak)) char *strchr(const char *text, int character)
{
    while (*text) {
        if (*text == (char)character)
            return (char *)text;
        text++;
    }
    return character == 0 ? (char *)text : NULL;
}

#undef strrchr
char *strrchr(const char *text, int character)
{
    const char *last = NULL;
    do {
        if (*text == (char)character)
            last = text;
    } while (*text++);
    return (char *)last;
}

/* Weak so Aqua can keep the copies in string_extra.c.  The FreeType smoke
 * test links this file without that translation unit. */
__attribute__((weak)) void *memchr(const void *memory, int value, size_t length)
{
    const unsigned char *bytes = memory;
    for (size_t i = 0; i < length; i++)
        if (bytes[i] == (unsigned char)value)
            return (void *)(bytes + i);
    return 0;
}

__attribute__((weak)) char *strcat(char *destination, const char *source)
{
    char *result = destination;
    while (*destination)
        destination++;
    while ((*destination++ = *source++))
        ;
    return result;
}

__attribute__((weak)) char *strncpy(char *destination, const char *source,
                                    size_t count)
{
    size_t i = 0;
    for (; i < count && source[i]; i++)
        destination[i] = source[i];
    for (; i < count; i++)
        destination[i] = 0;
    return destination;
}
