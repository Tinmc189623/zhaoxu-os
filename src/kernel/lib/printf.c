#include <console.h>
#include <kprintf.h>
#include <stdarg.h>
#include <string.h>
#include <types.h>

struct out_buffer {
    char *data;
    usize capacity;
    usize length;
};

static void out_char(struct out_buffer *out, char c)
{
    if (out->length + 1 < out->capacity)
        out->data[out->length] = c;
    out->length++;
}

static void out_repeat(struct out_buffer *out, char c, int count)
{
    while (count-- > 0)
        out_char(out, c);
}

static void out_string(struct out_buffer *out, const char *s, int width,
                       int left)
{
    int len = s ? (int)strlen(s) : 6;
    if (!s)
        s = "(null)";
    if (!left)
        out_repeat(out, ' ', width - len);
    while (*s)
        out_char(out, *s++);
    if (left)
        out_repeat(out, ' ', width - len);
}

static void out_unsigned(struct out_buffer *out, u64 value, unsigned base,
                         int width, int zero, int left, int upper,
                         int precision, int prefix)
{
    char digits[32];
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int count = 0;

    if (value == 0 && precision != 0) {
        digits[count++] = '0';
    } else {
        while (value) {
            digits[count++] = alphabet[value % base];
            value /= base;
        }
    }
    while (count < precision) {
        digits[count++] = '0';
    }

    int total = count;
    if (prefix == 1)
        total += 2;
    int padding = width - total;

    if (!left && !zero)
        out_repeat(out, ' ', padding);
    if (prefix == 1) {
        out_char(out, '0');
        out_char(out, upper ? 'X' : 'x');
    }
    if (!left && zero)
        out_repeat(out, '0', padding);
    while (count > 0)
        out_char(out, digits[--count]);
    if (left)
        out_repeat(out, ' ', padding);
}

int kvsnprintf(char *out, usize capacity, const char *fmt, va_list ap)
{
    struct out_buffer buffer = { out, capacity, 0 };
    const char *p = fmt;

    if (capacity > 0)
        out[0] = '\0';

    while (*p) {
        if (*p != '%') {
            out_char(&buffer, *p++);
            continue;
        }

        p++;
        if (*p == '%') {
            out_char(&buffer, '%');
            p++;
            continue;
        }

        int left = 0, zero = 0, plus = 0, space = 0, alternate = 0;
        for (;;) {
            if (*p == '-') left = 1;
            else if (*p == '0') zero = 1;
            else if (*p == '+') plus = 1;
            else if (*p == ' ') space = 1;
            else if (*p == '#') alternate = 1;
            else break;
            p++;
        }

        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            p++;
        } else {
            while (*p >= '0' && *p <= '9')
                width = width * 10 + (*p++ - '0');
        }

        int precision = -1;
        if (*p == '.') {
            p++;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(ap, int);
                p++;
            } else {
                while (*p >= '0' && *p <= '9')
                    precision = precision * 10 + (*p++ - '0');
            }
        }

        int long_count = 0;
        if (*p == 'l') {
            long_count = 1;
            p++;
            if (*p == 'l') {
                long_count = 2;
                p++;
            }
        } else if (*p == 'z') {
            long_count = 2;
            p++;
        } else if (*p == 'h') {
            p++;
            if (*p == 'h')
                p++;
        }

        char spec = *p ? *p++ : 0;
        switch (spec) {
        case 'c':
            out_char(&buffer, (char)va_arg(ap, int));
            break;
        case 's':
            out_string(&buffer, va_arg(ap, const char *), width, left);
            break;
        case 'd':
        case 'i': {
            i64 value;
            if (long_count == 2)
                value = va_arg(ap, i64);
            else if (long_count == 1)
                value = va_arg(ap, long);
            else
                value = va_arg(ap, int);
            u64 magnitude = (u64)value;
            int negative = value < 0;
            if (negative)
                magnitude = (u64)(-(value + 1)) + 1;
            char digits[32];
            int count = 0;
            if (magnitude == 0) {
                digits[count++] = '0';
            } else {
                while (magnitude) {
                    digits[count++] = (char)('0' + magnitude % 10);
                    magnitude /= 10;
                }
            }
            while (precision > count) {
                digits[count++] = '0';
            }
            int sign = negative ? '-' : (plus ? '+' : (space ? ' ' : 0));
            int total = count + (sign ? 1 : 0);
            int padding = width - total;
            if (!left && !zero)
                out_repeat(&buffer, ' ', padding);
            if (sign)
                out_char(&buffer, (char)sign);
            if (!left && zero)
                out_repeat(&buffer, '0', padding);
            while (count > 0)
                out_char(&buffer, digits[--count]);
            if (left)
                out_repeat(&buffer, ' ', padding);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            u64 value;
            if (long_count == 2)
                value = va_arg(ap, u64);
            else if (long_count == 1)
                value = va_arg(ap, unsigned long);
            else
                value = va_arg(ap, unsigned int);
            unsigned base = spec == 'o' ? 8 : (spec == 'u' ? 10 : 16);
            out_unsigned(&buffer, value, base, width, zero, left,
                         spec == 'X', precision < 0 ? 1 : precision, 0);
            break;
        }
        case 'p': {
            u64 value = (u64)(uintptr_t)va_arg(ap, void *);
            out_unsigned(&buffer, value, 16, 16, 1, 0, 0, 1, 1);
            break;
        }
        default:
            out_char(&buffer, '%');
            if (spec)
                out_char(&buffer, spec);
            break;
        }
    }

    if (buffer.capacity > 0) {
        usize index = buffer.length < buffer.capacity - 1
                          ? buffer.length
                          : buffer.capacity - 1;
        buffer.data[index] = '\0';
    }
    return (int)buffer.length;
}

int ksnprintf(char *out, usize capacity, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int result = kvsnprintf(out, capacity, fmt, ap);
    va_end(ap);
    return result;
}

int kprintf(const char *fmt, ...)
{
    char buffer[1024];
    va_list ap;
    va_start(ap, fmt);
    int result = kvsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    console_write(buffer);
    return result;
}
