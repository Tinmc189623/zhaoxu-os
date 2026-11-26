#ifndef ZHAOXU_FBCON_H
#define ZHAOXU_FBCON_H

#include <boot.h>
#include <console.h>
#include <types.h>

/* 80x25 text console painted on the Limine framebuffer.  The setup disc has
 * no display service, so this is how its text-mode UI reaches the screen. */
int fbcon_init(const struct boot_info *boot);
void fbcon_putc(char c);
void fbcon_set_color(enum console_color fg, enum console_color bg);
void fbcon_clear(void);

#endif
