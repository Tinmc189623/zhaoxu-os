#ifndef ZHAOXU_BOOT_H
#define ZHAOXU_BOOT_H

#include <types.h>

#define BOOT_MAGIC 0x5A4F5331u

/* Keep in sync with src/boot/main.asm. */
struct boot_info {
    u64 magic;
    u64 framebuffer_base;
    u64 framebuffer_size;
    u32 width;
    u32 height;
    u32 pitch;              /* bytes per scanline */
    u32 pixel_format;       /* EFI_GRAPHICS_OUTPUT_PIXEL_FORMAT */
    u64 memory_map;         /* higher-half virtual pointer */
    u64 memory_map_size;
    u64 memory_map_desc_size;
    u32 memory_map_desc_version;
    u32 reserved0;
    u64 rsdp;
    u64 kernel_phys_base;
    u64 kernel_virt_base;
    u64 kernel_size;
    u64 kernel_map_size;
    u64 page_table_root;
    u64 flags;
    u64 font_phys;          /* preloaded WQY.TTC from the boot volume */
    u64 font_size;
    u64 wallpaper_phys;     /* preloaded default wallpaper (raw RGB888) */
    u64 wallpaper_size;
} __attribute__((packed));

#define BOOT_FLAG_HIGHER_HALF 1u

const struct boot_info *boot_get_info(void);

#endif
