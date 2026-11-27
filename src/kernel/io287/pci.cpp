/*
 * Zhaoxu OS - IO/287 PCI bus backend.
 *
 * Mechanism #1 configuration cycles (0xCF8/0xCFC) enumerate every bus,
 * device and function once at init and register each one under the IO/287
 * root node.  ECAM is a later backend; the registry does not care which one
 * produced the node.
 */
#include <io.h>
#include <io287.h>
#include <types.h>

#include "internal.h"

/* One aligned 32-bit configuration dword. */
static u32 config_read32(u32 bus, u32 slot, u32 function, u32 offset)
{
    u32 address = 0x80000000u | ((bus & 0xFF) << 16) |
                  ((slot & 0x1F) << 11) | ((function & 7) << 8) |
                  (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC | (offset & 3));
}

/* Append an unsigned decimal number; returns the new end pointer. */
static char *append_dec(char *out, u32 value)
{
    char digits[10];
    int count = 0;
    if (!value) {
        *out++ = '0';
        return out;
    }
    while (value) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count)
        *out++ = digits[--count];
    return out;
}

/* Append four lowercase hex digits. */
static char *append_hex4(char *out, u32 value)
{
    static const char hex[] = "0123456789abcdef";
    for (int shift = 12; shift >= 0; shift -= 4)
        *out++ = hex[(value >> shift) & 0xF];
    return out;
}

/* Register one function of one PCI device. */
static void add_function(u32 bus, u32 slot, u32 function, u32 id,
                         u32 class_code)
{
    char name[IO287_NAME_MAX];
    char compatible[IO287_TEXT_MAX];
    char *p = name;
    *p++ = 'p';
    *p++ = 'c';
    *p++ = 'i';
    p = append_dec(p, bus);
    *p++ = ':';
    p = append_dec(p, slot);
    *p++ = '.';
    p = append_dec(p, function);
    *p = 0;

    u32 node = io287_add_device(name, 0, IO287_BUS_PCI, bus, slot, function,
                                id & 0xFFFFu, id >> 16, class_code);
    if (node == IO287_NONE)
        return;

    io287_device_prop_u64(node, "vendor", id & 0xFFFFu);
    io287_device_prop_u64(node, "device", id >> 16);
    io287_device_prop_u64(node, "class", class_code);

    char *c = compatible;
    *c++ = 'p';
    *c++ = 'c';
    *c++ = 'i';
    *c++ = ',';
    c = append_hex4(c, id & 0xFFFFu);
    *c++ = ',';
    c = append_hex4(c, id >> 16);
    *c = 0;
    io287_device_prop_text(node, "compatible", compatible);
}

void io287_pci_enumerate(void)
{
    for (u32 bus = 0; bus < 256; bus++) {
        for (u32 slot = 0; slot < 32; slot++) {
            u32 id = config_read32(bus, slot, 0, 0x00);
            if (id == 0xFFFFFFFFu || (id & 0xFFFFu) == 0xFFFFu)
                continue;

            u32 header = (config_read32(bus, slot, 0, 0x0C) >> 16) & 0xFFu;
            u32 functions = (header & 0x80u) ? 8u : 1u;
            for (u32 function = 0; function < functions; function++) {
                u32 function_id = function
                                       ? config_read32(bus, slot, function, 0)
                                       : id;
                if ((function_id & 0xFFFFu) == 0xFFFFu)
                    continue;
                u32 class_code =
                    config_read32(bus, slot, function, 0x08) >> 8;
                add_function(bus, slot, function, function_id, class_code);
            }
        }
    }
}
