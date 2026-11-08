#ifndef ZHAOXU_CONSOLE_H
#define ZHAOXU_CONSOLE_H

#include <boot.h>
#include <types.h>

enum console_color {
    COLOR_BLACK = 0,
    COLOR_BLUE,
    COLOR_GREEN,
    COLOR_CYAN,
    COLOR_RED,
    COLOR_MAGENTA,
    COLOR_BROWN,
    COLOR_LIGHT_GRAY,
    COLOR_DARK_GRAY,
    COLOR_LIGHT_BLUE,
    COLOR_LIGHT_GREEN,
    COLOR_LIGHT_CYAN,
    COLOR_LIGHT_RED,
    COLOR_PINK,
    COLOR_YELLOW,
    COLOR_WHITE
};

void console_init(const struct boot_info *bi);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s);
void console_write_n(const char *s, usize length);
void console_set_color(enum console_color fg, enum console_color bg);
void console_reset_color(void);
u32 console_columns(void);
u32 console_rows(void);
int console_has_framebuffer(void);
int console_read_char(void);
int console_has_input(void);
void console_backspace(void);

#endif
