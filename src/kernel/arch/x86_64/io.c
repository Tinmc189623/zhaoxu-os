#include <io.h>

void outb_port(u16 port, u8 value) { outb(port, value); }
u8 inb_port(u16 port) { return inb(port); }
void outw_port(u16 port, u16 value) { outw(port, value); }
u16 inw_port(u16 port) { return inw(port); }
void outl_port(u16 port, u32 value) { outl(port, value); }
u32 inl_port(u16 port) { return inl(port); }

/* Volatile MMIO primitives for the Ada/SPARK drivers.  Ada's ordinary
 * pointer dereference is not volatile, so the compiler may hoist repeated
 * register reads out of poll loops; these wrappers guarantee one access
 * per call. */
u8 mmio_read8(u64 address) { return *(volatile u8 *)(uintptr_t)address; }
u16 mmio_read16(u64 address) { return *(volatile u16 *)(uintptr_t)address; }
u32 mmio_read32(u64 address) { return *(volatile u32 *)(uintptr_t)address; }
u64 mmio_read64(u64 address) { return *(volatile u64 *)(uintptr_t)address; }
void mmio_write8(u64 address, u8 value) { *(volatile u8 *)(uintptr_t)address = value; }
void mmio_write16(u64 address, u16 value) { *(volatile u16 *)(uintptr_t)address = value; }
void mmio_write32(u64 address, u32 value) { *(volatile u32 *)(uintptr_t)address = value; }
void mmio_write64(u64 address, u64 value) { *(volatile u64 *)(uintptr_t)address = value; }
