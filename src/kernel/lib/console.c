/*
 * Zhaoxu OS - kernel console.
 *
 * The kernel has no hardware console: log bytes go to a ring drained by the
 * Ring3 driver service and printed on the serial port.  Keyboard input comes
 * back through the driver input page.  DEBUG_SERIAL=1 adds a development-only
 * UART mirror; the shipped build keeps it compiled out.
 */
#include <boot.h>
#include <console.h>
#include <dev.h>
#include <io.h>
#include <klog.h>
#include <string.h>
#include <types.h>

static void (*output_sink)(char c);
static void (*color_sink)(enum console_color fg, enum console_color bg);
/* Set once the setup disc's framebuffer console owns the screen: kernel
 * diagnostics keep going to the log ring and serial but no longer overwrite
 * the installer's text. */
static int kernel_screen_muted;

#if DEBUG_SERIAL
static void debug_serial(const char *text, usize length)
{
    for (usize i = 0; i < length; i++) {
        char c = text[i];
        if (c == '\n') {
            while ((inb(0x3F8 + 5) & 0x20) == 0)
                ;
            outb(0x3F8, '\r');
        }
        while ((inb(0x3F8 + 5) & 0x20) == 0)
            ;
        outb(0x3F8, (u8)c);
    }
}
#else
static void debug_serial(const char *text, usize length)
{
    (void)text;
    (void)length;
}
#endif

void console_init(const struct boot_info *bi)
{
    (void)bi;
    output_sink = NULL;
}

void console_clear(void)
{
}

void console_putc(char c)
{
    if (output_sink)
        output_sink(c);
    debug_serial(&c, 1);
    klog_putc(c);
    dev_log_write(&c, 1);
}

/* Deliver bytes to the screen sink (optionally), the debug UART, the kernel
 * log ring and the Ring3 log page. */
static void emit(const char *s, usize length, int to_screen)
{
    if (to_screen && output_sink) {
        for (usize i = 0; i < length; i++)
            output_sink(s[i]);
    }
    debug_serial(s, length);
    for (usize i = 0; i < length; i++)
        klog_putc(s[i]);
    dev_log_write(s, length);
}

void console_write(const char *s)
{
    if (!s)
        return;
    console_write_n(s, strlen(s));
}

void console_write_n(const char *s, usize length)
{
    emit(s, length, 1);
}

/* kprintf path: the screen is skipped while the installer owns it. */
void console_write_kernel(const char *s)
{
    if (!s)
        return;
    emit(s, strlen(s), kernel_screen_muted ? 0 : 1);
}

void console_set_kernel_mute(int muted)
{
    kernel_screen_muted = muted;
}

void console_set_color(enum console_color fg, enum console_color bg)
{
    if (color_sink)
        color_sink(fg, bg);
}

void console_reset_color(void)
{
    if (color_sink)
        color_sink(COLOR_LIGHT_GRAY, COLOR_BLACK);
}

void console_set_sink(void (*sink)(char c))
{
    output_sink = sink;
}

void console_set_color_sink(void (*sink)(enum console_color fg,
                                         enum console_color bg))
{
    color_sink = sink;
}

u32 console_columns(void)
{
    return 80;
}

u32 console_rows(void)
{
    return 25;
}

int console_has_framebuffer(void)
{
    const struct boot_info *boot = boot_get_info();
    return boot && boot->framebuffer_base != 0;
}

int console_read_char(void)
{
    return dev_key_pop();
}

int console_has_input(void)
{
    return dev_key_has_input();
}

void console_backspace(void)
{
    console_write("\b \b");
}
