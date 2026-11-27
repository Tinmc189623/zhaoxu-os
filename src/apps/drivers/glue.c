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

void console_write_dec(u32 value);

/* PCI enumeration output.  pci.adb calls these while walking the bus. */
static void serial_write_hex(u32 value, int digits)
{
    static const char hex[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; i--)
        serial_putc_raw(hex[(value >> ((u32)i * 4)) & 0xF]);
}

void pci_report_header(void)
{
    console_write("[pci] enumeration\n");
}

void pci_report_device(u32 bus, u32 slot, u32 func, u32 id, u32 class_code)
{
    console_write("[pci] ");
    serial_write_hex(bus, 2);
    serial_putc_raw(':');
    serial_write_hex(slot, 2);
    serial_putc_raw('.');
    serial_write_hex(func, 1);
    console_write(" ");
    serial_write_hex(id & 0xFFFF, 4);
    serial_putc_raw(':');
    serial_write_hex(id >> 16, 4);
    console_write(" class ");
    serial_write_hex(class_code >> 8, 6);
    serial_putc_raw('\r');
    serial_putc_raw('\n');
}

void pci_report_summary(u32 found, u32 ecam)
{
    console_write("[pci] ");
    console_write_dec(found);
    console_write(" device(s), ECAM ");
    console_write(ecam ? "on\n" : "off\n");
}

/* Print an unsigned decimal value; the driver service has no printf. */
void console_write_dec(u32 value)
{
    char digits[10];
    int count = 0;
    if (value == 0) {
        serial_putc_raw('0');
        return;
    }
    while (value) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count)
        serial_putc_raw(digits[--count]);
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
