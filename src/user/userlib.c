#include "userlib.h"

/* Weak: services that also link kernel/lib/string.c (e.g. /bin/ext4fs, which
 * needs strlen/strcmp for the ext4 core) must not collide with these.  A
 * single-program link still gets exactly one definition, so behaviour is
 * unchanged for every existing executable. */
__attribute__((weak)) void *memset(void *destination, int value, u64 length)
{
    u8 *bytes = (u8 *)destination;
    while (length--)
        *bytes++ = (u8)value;
    return destination;
}

__attribute__((weak)) void *memcpy(void *destination, const void *source,
                                   u64 length)
{
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;
    while (length--)
        *out++ = *in++;
    return destination;
}

__attribute__((weak)) void *memmove(void *destination, const void *source,
                                    u64 length)
{
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;
    if (out < in) {
        while (length--)
            *out++ = *in++;
    } else {
        out += length;
        in += length;
        while (length--)
            *--out = *--in;
    }
    return destination;
}

u64 u_strlen(const char *text)
{
    u64 length = 0;
    while (text[length])
        length++;
    return length;
}

int u_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

void u_puts(const char *text)
{
    u64 length = u_strlen(text);
    if (length)
        sys_write(1, text, length);
}

void u_putn(long value)
{
    char buffer[24];
    int index = 0;
    unsigned long number;

    if (value < 0) {
        sys_write(1, "-", 1);
        number = (unsigned long)(-value);
    } else {
        number = (unsigned long)value;
    }

    if (number == 0) {
        sys_write(1, "0", 1);
        return;
    }

    while (number > 0) {
        buffer[index++] = (char)('0' + number % 10);
        number /= 10;
    }
    for (int i = 0; i < index / 2; i++) {
        char temporary = buffer[i];
        buffer[i] = buffer[index - 1 - i];
        buffer[index - 1 - i] = temporary;
    }
    sys_write(1, buffer, (u64)index);
}

void u_puthex(u64 value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[18];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 16; i++)
        buffer[2 + i] = digits[(value >> (60 - i * 4)) & 0xF];
    sys_write(1, buffer, sizeof(buffer));
}

int user_main(long *stack);

__asm__(
    ".global _start\n"
    ".type _start, @function\n"
    "_start:\n"
    "    movq %rsp, %rdi\n"
    "    andq $-16, %rsp\n"
    "    call user_main\n"
    "    movl %eax, %edi\n"
    "    movl $0, %eax\n"
    "    int $0x80\n"
    "1:  hlt\n"
    "    jmp 1b\n");
