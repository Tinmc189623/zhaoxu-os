#include <idt.h>
#include <io.h>

void pic_init(void)
{
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void pic_mask(u8 irq)
{
    u16 port = (irq < 8) ? 0x21 : 0xA1;
    u8 bit = (u8)(1u << (irq & 7));
    outb(port, (u8)(inb(port) | bit));
}

void pic_unmask(u8 irq)
{
    u16 port = (irq < 8) ? 0x21 : 0xA1;
    u8 bit = (u8)(1u << (irq & 7));
    outb(port, (u8)(inb(port) & ~bit));
}

void pic_eoi(u8 irq)
{
    if (irq >= 8)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}
