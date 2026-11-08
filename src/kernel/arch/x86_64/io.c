#include <io.h>

void outb_port(u16 port, u8 value) { outb(port, value); }
u8 inb_port(u16 port) { return inb(port); }
void outw_port(u16 port, u16 value) { outw(port, value); }
u16 inw_port(u16 port) { return inw(port); }
void outl_port(u16 port, u32 value) { outl(port, value); }
u32 inl_port(u16 port) { return inl(port); }
