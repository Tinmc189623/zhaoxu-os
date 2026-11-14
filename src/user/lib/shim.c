/*
 * Zhaoxu OS - ring 3 compatibility layer for the ADE display server.
 *
 * Kernel-facing helpers used by the shared desktop sources (console input,
 * mouse, clock, RTC, printf) are mapped onto ZOS API system calls here.
 */
#include <stdarg.h>
#include <console.h>
#include <drivers.h>
#include <kprintf.h>
#include <rtc.h>
#include <smp.h>
#include <string.h>
#include <timer.h>
#include <types.h>
#include <zos_user.h>

#include "../userlib.h"

#define FONT_SHARED_BYTES (8u * 1024u * 1024u)
#define WALLPAPER_SHARED_BYTES (8u * 1024u * 1024u)

static void emit_char(char *buffer, usize capacity, usize *length, char value)
{
    if (*length + 1 < capacity)
        buffer[*length] = value;
    (*length)++;
}

static void emit_text(char *buffer, usize capacity, usize *length,
                      const char *text)
{
    while (*text)
        emit_char(buffer, capacity, length, *text++);
}

static void emit_number(char *buffer, usize capacity, usize *length,
                        u64 value, u32 base, int width, char pad,
                        int uppercase)
{
    char digits[32];
    const char *alphabet = uppercase ? "0123456789ABCDEF"
                                     : "0123456789abcdef";
    int count = 0;

    if (value == 0) {
        digits[count++] = '0';
    } else {
        while (value) {
            digits[count++] = alphabet[value % base];
            value /= base;
        }
    }
    for (int i = count; i < width; i++)
        emit_char(buffer, capacity, length, pad);
    while (count > 0)
        emit_char(buffer, capacity, length, digits[--count]);
}

static int vformat(char *buffer, usize capacity, const char *format,
                   va_list arguments)
{
    usize length = 0;

    if (!buffer || capacity == 0 || !format)
        return 0;

    while (*format) {
        if (*format != '%') {
            emit_char(buffer, capacity, &length, *format++);
            continue;
        }
        format++;

        char pad = ' ';
        int width = 0;
        if (*format == '0') {
            pad = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        int long_count = 0;
        while (*format == 'l') {
            long_count++;
            format++;
        }

        char spec = *format ? *format++ : 0;
        if (spec == 's') {
            emit_text(buffer, capacity, &length,
                      va_arg(arguments, const char *));
        } else if (spec == 'c') {
            emit_char(buffer, capacity, &length,
                      (char)va_arg(arguments, int));
        } else if (spec == 'd') {
            long long value = (long_count >= 2)
                                  ? va_arg(arguments, long long)
                                  : (long_count == 1)
                                        ? (long long)va_arg(arguments, long)
                                        : (long long)va_arg(arguments, int);
            if (value < 0) {
                emit_char(buffer, capacity, &length, '-');
                value = -value;
            }
            emit_number(buffer, capacity, &length, (u64)value, 10, width,
                        pad, 0);
        } else if (spec == 'u') {
            u64 value = (long_count >= 2)
                            ? va_arg(arguments, unsigned long long)
                            : (long_count == 1)
                                  ? (u64)va_arg(arguments, unsigned long)
                                  : (u64)va_arg(arguments, unsigned int);
            emit_number(buffer, capacity, &length, value, 10, width, pad, 0);
        } else if (spec == 'x' || spec == 'X') {
            u64 value = (long_count >= 2)
                            ? va_arg(arguments, unsigned long long)
                            : (long_count == 1)
                                  ? (u64)va_arg(arguments, unsigned long)
                                  : (u64)va_arg(arguments, unsigned int);
            emit_number(buffer, capacity, &length, value, 16, width, pad,
                        spec == 'X');
        } else if (spec == '%') {
            emit_char(buffer, capacity, &length, '%');
        } else if (spec) {
            emit_char(buffer, capacity, &length, spec);
        }
    }

    if (length >= capacity)
        length = capacity - 1;
    buffer[length] = 0;
    return (int)length;
}

int ksnprintf(char *buffer, usize capacity, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int length = vformat(buffer, capacity, format, arguments);
    va_end(arguments);
    return length;
}

void kprintf(const char *format, ...)
{
    char buffer[512];
    va_list arguments;

    va_start(arguments, format);
    int length = vformat(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    if (length > 0)
        sys_serial_write(buffer, (u64)length);
}

static int g_mouse_x;
static int g_mouse_y;
static int g_mouse_buttons;
static int g_mouse_ready;

static void refresh_mouse(void)
{
    unsigned long state[3];
    long status = sys_mouse(state);
    if (status >= 0) {
        g_mouse_x = (int)state[0];
        g_mouse_y = (int)state[1];
        g_mouse_buttons = (int)state[2];
        g_mouse_ready = status > 0;
    }
}

int mouse_x(void)
{
    refresh_mouse();
    return g_mouse_x;
}

int mouse_y(void)
{
    refresh_mouse();
    return g_mouse_y;
}

int mouse_buttons(void)
{
    refresh_mouse();
    return g_mouse_buttons;
}

int mouse_ready(void)
{
    refresh_mouse();
    return g_mouse_ready;
}

int console_read_char(void)
{
    return (int)sys_key();
}

u64 timer_ticks(void)
{
    return (u64)sys_uptime();
}

void timer_sleep_ms(u32 milliseconds)
{
    sys_sleep(milliseconds);
}

int rtc_read(struct rtc_time *time)
{
    return sys_rtc(time) == 0 ? 0 : -1;
}

u32 smp_cpu_count(void)
{
    return (u32)sys_cpus();
}

u64 zos_alloc_pages(u32 pages)
{
    long id = sys_shm_create((unsigned long)pages * 4096UL);
    if (id <= 0)
        return 0;
    long address = sys_shm_map(id);
    return address > 0 ? (u64)address : 0;
}

u8 *zos_load_font(u64 *size_out)
{
    static const char *const candidates[] = {
        "/mnt/MiSans.ttf", "/mnt/MiSans-Regular.ttf",
        "/mnt/MiSansGlobal.ttf", "/mnt/MiSansLatinVF.ttf",
        "/mnt/MiSansLatin-Regular.ttf", "/mnt/Inter.ttf",
        "/mnt/Inter-Regular.ttf", "/mnt/WQY.TTC"
    };

    if (size_out)
        *size_out = 0;

    long shm = sys_shm_create(FONT_SHARED_BYTES);
    if (shm <= 0)
        return 0;
    u8 *buffer = (u8 *)sys_shm_map(shm);
    if (!buffer)
        return 0;

    for (u32 i = 0; i < (u32)(sizeof(candidates) / sizeof(candidates[0]));
         i++) {
        long total = sys_read_path(candidates[i], buffer, FONT_SHARED_BYTES);
        if (total > 12) {
            kprintf("[ade ] font %s: %ld bytes\n", candidates[i], total);
            if (size_out)
                *size_out = (u64)total;
            return buffer;
        }
    }

    long boot_total = sys_boot_font(buffer, FONT_SHARED_BYTES);
    if (boot_total > 12) {
        kprintf("[ade ] font boot volume: %ld bytes\n", boot_total);
        if (size_out)
            *size_out = (u64)boot_total;
        return buffer;
    }
    return 0;
}

u8 *zos_load_wallpaper(u64 *size_out)
{
    if (size_out)
        *size_out = 0;

    long shm = sys_shm_create(WALLPAPER_SHARED_BYTES);
    if (shm <= 0)
        return 0;
    u8 *buffer = (u8 *)sys_shm_map(shm);
    if (!buffer)
        return 0;

    long total = sys_read_path("/mnt/WALLPAPER.RAW", buffer,
                               WALLPAPER_SHARED_BYTES);
    if (total < 1920L * 1080L * 3L) {
        /* The live ISO has no ZHAOXUDATA volume; the boot loader already
         * staged the rule-33 frame from boot/zhaoxu/WALL.RGB. */
        total = sys_boot_wallpaper(buffer, WALLPAPER_SHARED_BYTES);
        if (total < 1920L * 1080L * 3L) {
            kprintf("[ade ] wallpaper: no WALLPAPER.RAW or boot frame\n");
            return 0;
        }
        kprintf("[ade ] wallpaper boot volume: %ld bytes\n", total);
    } else {
        kprintf("[ade ] wallpaper /mnt/WALLPAPER.RAW: %ld bytes\n", total);
    }
    if (size_out)
        *size_out = (u64)total;
    return buffer;
}
