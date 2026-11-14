/*
 * Zhaoxu OS - C glue for the Ring3 Ada drivers.
 *
 * The service has an identity map of the first 4 GiB (U/S=1), so MMIO and
 * DMA pointers are physical addresses, and IOPL=3 lets the driver use the
 * legacy port I/O the hardware requires.
 */
#include <io.h>
#include <types.h>
#include <userlib.h>

#define COM1 0x3F8

u8 inb_port(u16 port)
{
    return inb(port);
}

void outb_port(u16 port, u8 value)
{
    outb(port, value);
}

void outw_port(u16 port, u16 value)
{
    outw(port, value);
}

u16 inw_port(u16 port)
{
    return inw(port);
}

void outl_port(u16 port, u32 value)
{
    outl(port, value);
}

u32 inl_port(u16 port)
{
    return inl(port);
}

u8 mmio_read8(u64 address)
{
    return *(volatile u8 *)(uintptr_t)address;
}

u16 mmio_read16(u64 address)
{
    return *(volatile u16 *)(uintptr_t)address;
}

u32 mmio_read32(u64 address)
{
    return *(volatile u32 *)(uintptr_t)address;
}

u64 mmio_read64(u64 address)
{
    return *(volatile u64 *)(uintptr_t)address;
}

void mmio_write8(u64 address, u8 value)
{
    *(volatile u8 *)(uintptr_t)address = value;
}

void mmio_write16(u64 address, u16 value)
{
    *(volatile u16 *)(uintptr_t)address = value;
}

void mmio_write32(u64 address, u32 value)
{
    *(volatile u32 *)(uintptr_t)address = value;
}

void mmio_write64(u64 address, u64 value)
{
    *(volatile u64 *)(uintptr_t)address = value;
}

/* The xHCI driver zeroes freshly allocated ring and context pages.  Keep
 * the loop byte-wise and volatile so it never grows a libc memset call into
 * the freestanding driver process. */
void buffer_zero(void *destination, u32 bytes)
{
    volatile u8 *dst = (volatile u8 *)destination;
    while (bytes--)
        *dst++ = 0;
}

void usb_delay_ms(u32 milliseconds)
{
    sys_sleep(milliseconds);
}

u64 pmm_alloc_page(void)
{
    return sys_dev_alloc_page();
}

u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset)
{
    u32 address = 0x80000000u | ((bus & 0xFF) << 16) |
                  ((slot & 0x1F) << 11) | ((function & 7) << 8) |
                  (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC + (offset & 3));
}

void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                        u32 value)
{
    u32 address = 0x80000000u | ((bus & 0xFF) << 16) |
                  ((slot & 0x1F) << 11) | ((function & 7) << 8) |
                  (offset & 0xFC);
    outl(0xCF8, address);
    outl(0xCFC + (offset & 3), value);
}

static void serial_putc_raw(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0)
        ;
    outb(COM1, (u8)c);
}

void console_write(const char *text)
{
    if (!text)
        return;
    while (*text) {
        if (*text == '\n')
            serial_putc_raw('\r');
        serial_putc_raw(*text++);
    }
}

void console_write_n(const char *text, usize length)
{
    for (usize i = 0; i < length; i++)
        serial_putc_raw(text[i]);
}

usize strlen(const char *text)
{
    usize length = 0;
    while (text && text[length])
        length++;
    return length;
}

int toupper(int c)
{
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    return c;
}
