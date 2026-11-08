#ifndef ZHAOXU_IO_H
#define ZHAOXU_IO_H

#include <types.h>

static inline void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(u16 port, u16 value)
{
    __asm__ volatile("outw %0, %1" :: "a"(value), "Nd"(port));
}

static inline u16 inw(u16 port)
{
    u16 value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(u16 port, u32 value)
{
    __asm__ volatile("outl %0, %1" :: "a"(value), "Nd"(port));
}

static inline u32 inl(u16 port)
{
    u32 value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }
static inline void pause(void) { __asm__ volatile("pause"); }

/* Non-static wrappers are used by the Ada drivers (C convention). */
void outb_port(u16 port, u8 value);
u8 inb_port(u16 port);
void outw_port(u16 port, u16 value);
u16 inw_port(u16 port);
void outl_port(u16 port, u32 value);
u32 inl_port(u16 port);

#endif
