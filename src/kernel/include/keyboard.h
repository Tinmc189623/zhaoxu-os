#ifndef ZHAOXU_KEYBOARD_H
#define ZHAOXU_KEYBOARD_H

#include <types.h>

void keyboard_init(void);
void keyboard_irq(void);
int keyboard_getchar(void);
int keyboard_has_input(void);
void keyboard_push_char(char c);

#endif
