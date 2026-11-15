/*
 * Zhaoxu OS - Limine boot protocol bridge.
 *
 * Limine loads kernel.elf at its link address with its own page tables.  The
 * rest of Yuanshuo assumes an identity mapping of physical memory, so this
 * file installs a bootstrap identity map, turns Limine's responses into the
 * existing struct boot_info, and hands control to kernel_main.
 */
#include <boot.h>
#include <types.h>

#include "limine.h"

#define KERNEL_VMA 0xffffffff80000000ULL
#define KERNEL_STACK_TOP 0xffffffff80f00000ULL
#define BOOT_IDENTITY_LIMIT (64ULL * 1024 * 1024 * 1024)
#define BOOT_MEMMAP_MAX 256

extern char __kernel_end[];

/* Requests live in a writable loaded section; Limine fills the pointers. */
#define LIMINE_REQUEST(lower, upper)                                           \
    __attribute__((used, section(".limine_requests")))                         \
    static volatile struct limine_##lower##_request g_limine_##lower = {       \
        .id = LIMINE_##upper##_REQUEST_ID, .revision = 0, .response = 0}

__attribute__((used, section(".limine_requests")))
static volatile u64 g_limine_base_revision[3] = LIMINE_BASE_REVISION(3);

LIMINE_REQUEST(bootloader_info, BOOTLOADER_INFO);
LIMINE_REQUEST(firmware_type, FIRMWARE_TYPE);
LIMINE_REQUEST(hhdm, HHDM);
LIMINE_REQUEST(framebuffer, FRAMEBUFFER);
LIMINE_REQUEST(memmap, MEMMAP);
LIMINE_REQUEST(rsdp, RSDP);
LIMINE_REQUEST(executable_address, EXECUTABLE_ADDRESS);
LIMINE_REQUEST(executable_file, EXECUTABLE_FILE);
LIMINE_REQUEST(module, MODULE);

struct boot_memory_descriptor {
    u32 type;
    u32 pad;
    u64 physical_start;
    u64 virtual_start;
    u64 number_of_pages;
    u64 attribute;
} __attribute__((packed));

static struct boot_info g_limine_boot;
static struct boot_memory_descriptor g_boot_memmap[BOOT_MEMMAP_MAX];
static u64 g_limine_pml4[512] __attribute__((aligned(4096)));
static u64 g_limine_pdpt[512] __attribute__((aligned(4096)));
static u64 g_limine_pds[64][512] __attribute__((aligned(4096)));
static u64 g_limine_stack[0x200000 / sizeof(u64)]
    __attribute__((aligned(4096)));
static u64 g_limine_stack_pt[512] __attribute__((aligned(4096)));

/* Translate an HHDM virtual address returned by Limine into a physical one. */
static u64 limine_physical(u64 address, u64 hhdm)
{
    if (address >= hhdm && address < hhdm + BOOT_IDENTITY_LIMIT)
        return address - hhdm;
    return address;
}

/* Compare the final path component of a module path. */
static int module_name_is(const char *path, const char *name)
{
    if (!path || !name)
        return 0;
    const char *base = path;
    for (const char *cursor = path; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\')
            base = cursor + 1;
    }
    for (int i = 0;; i++) {
        char left = base[i];
        char right = name[i];
        if (left >= 'A' && left <= 'Z')
            left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z')
            right = (char)(right - 'A' + 'a');
        if (left != right)
            return 0;
        if (!left)
            return 1;
    }
}

/* Convert one kernel virtual address into the physical base Limine chose. */
static u64 kernel_physical(u64 virtual, u64 physical_base,
                           u64 virtual_base)
{
    return physical_base + (virtual - virtual_base);
}

/* Turn the Limine memory map into the UEFI descriptors the PMM expects. */
static void build_memory_map(struct limine_memmap_response *source, u64 hhdm)
{
    u64 count = 0;
    for (u64 i = 0; i < source->entry_count && count < BOOT_MEMMAP_MAX; i++) {
        struct limine_memmap_entry *entry = source->entries[i];
        if (!entry || entry->length == 0)
            continue;
        struct boot_memory_descriptor *out = &g_boot_memmap[count++];
        out->type = entry->type == LIMINE_MEMMAP_USABLE ? 7 : 2;
        out->pad = 0;
        out->physical_start = entry->base;
        out->virtual_start = 0;
        out->number_of_pages = (entry->length + 4095) / 4096;
        out->attribute = 0;
    }
    g_limine_boot.memory_map = (u64)(uintptr_t)g_boot_memmap;
    g_limine_boot.memory_map_size = count * sizeof(g_boot_memmap[0]);
    g_limine_boot.memory_map_desc_size = sizeof(g_boot_memmap[0]);
    g_limine_boot.memory_map_desc_version = 1;
    (void)hhdm;
}

/* Find the module used for installation or the desktop assets. */
static struct limine_file *find_module(const char *name)
{
    struct limine_module_response *response = g_limine_module.response;
    if (!response)
        return 0;
    for (u64 i = 0; i < response->module_count; i++) {
        struct limine_file *file = response->modules[i];
        if (file && module_name_is(file->path, name))
            return file;
    }
    return 0;
}

/* Install a 2 MiB identity map in PML4 slot 0 and keep every high-half map. */
static int install_identity_map(u64 hhdm, u64 physical_base,
                                u64 virtual_base)
{
    u64 current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));
    u64 *current = (u64 *)(uintptr_t)(hhdm + (current_cr3 & ~0xFFFULL));
    if (!current)
        return -1;

    for (int i = 1; i < 512; i++)
        g_limine_pml4[i] = current[i];

    u64 pdpt_physical = kernel_physical((u64)(uintptr_t)g_limine_pdpt,
                                        physical_base, virtual_base);
    for (int gigabyte = 0; gigabyte < 64; gigabyte++) {
        u64 pd_physical = kernel_physical((u64)(uintptr_t)g_limine_pds[gigabyte],
                                          physical_base, virtual_base);
        for (int entry = 0; entry < 512; entry++) {
            u64 address = (u64)gigabyte * 0x40000000ULL +
                          (u64)entry * 0x200000ULL;
            g_limine_pds[gigabyte][entry] = address | 0x83; /* P | W | PS */
        }
        g_limine_pdpt[gigabyte] = pd_physical | 0x03;
    }
    g_limine_pml4[0] = pdpt_physical | 0x03;

    /* Limine may not have mapped the linker's reserved 15 MiB stack window.
     * Point the stack virtual range at a 2 MiB array inside the kernel BSS,
     * which the PMM already reserves as part of the kernel image. */
    u64 pml4_index = (KERNEL_STACK_TOP >> 39) & 0x1FF;
    u64 pdpt_index = (KERNEL_STACK_TOP >> 30) & 0x1FF;
    u64 pd_index = (KERNEL_STACK_TOP >> 21) & 0x1FF;
    u64 pml4_entry = g_limine_pml4[pml4_index];
    if (!(pml4_entry & 1))
        return -1;
    u64 *pdpt = (u64 *)(uintptr_t)(hhdm + (pml4_entry & ~0xFFFULL));
    u64 pdpte = pdpt[pdpt_index];
    if (!(pdpte & 1))
        return -1;
    if (pdpte & 0x80) {
        /* A 1 GiB huge page over the kernel is not what Limine produces; be
         * explicit rather than corrupting the map. */
        return -1;
    }
    u64 *pd = (u64 *)(uintptr_t)(hhdm + (pdpte & ~0xFFFULL));

    u64 stack_physical = kernel_physical((u64)(uintptr_t)g_limine_stack,
                                         physical_base, virtual_base);
    u64 stack_pt_physical = kernel_physical((u64)(uintptr_t)g_limine_stack_pt,
                                            physical_base, virtual_base);
    if (!(pd[pd_index] & 1) || (pd[pd_index] & 0x80)) {
        pd[pd_index] = stack_pt_physical | 0x03;
    } else {
        u64 *pt = (u64 *)(uintptr_t)(hhdm + (pd[pd_index] & ~0xFFFULL));
        for (int entry = 0; entry < 512; entry++)
            pt[entry] = 0;
        pd[pd_index] = stack_pt_physical | 0x03;
    }
    for (int entry = 0; entry < 512; entry++)
        g_limine_stack_pt[entry] =
            (stack_physical + (u64)entry * 0x1000) | 0x03;

    __asm__ volatile("mov %0, %%cr3" :: "r"(
        kernel_physical((u64)(uintptr_t)g_limine_pml4, physical_base,
                        virtual_base)) : "memory");
    return 0;
}

/* Fill boot_info from the Limine responses.  Returns NULL on failure. */
__attribute__((used)) struct boot_info *limine_boot_prepare(void)
{
    if (!LIMINE_BASE_REVISION_SUPPORTED(g_limine_base_revision) ||
        !g_limine_hhdm.response || !g_limine_framebuffer.response ||
        !g_limine_memmap.response || !g_limine_executable_address.response)
        return 0;

    u64 hhdm = g_limine_hhdm.response->offset;
    u64 physical_base = g_limine_executable_address.response->physical_base;
    u64 virtual_base = g_limine_executable_address.response->virtual_base;

    for (int i = 0; i < (int)sizeof(g_limine_boot); i++)
        ((u8 *)&g_limine_boot)[i] = 0;
    g_limine_boot.magic = BOOT_MAGIC;
    g_limine_boot.kernel_phys_base = physical_base;
    g_limine_boot.kernel_virt_base = virtual_base;
    g_limine_boot.kernel_size = g_limine_executable_file.response &&
                                        g_limine_executable_file.response
                                            ->executable_file
                                    ? g_limine_executable_file.response
                                          ->executable_file->size
                                    : 0;
    u64 image_end = (u64)(uintptr_t)__kernel_end - KERNEL_VMA;
    g_limine_boot.kernel_map_size = (image_end + 0x1FFFFF) & ~0x1FFFFFULL;
    g_limine_boot.flags = BOOT_FLAG_HIGHER_HALF;

    struct limine_framebuffer *framebuffer =
        g_limine_framebuffer.response->framebuffer_count
            ? g_limine_framebuffer.response->framebuffers[0]
            : 0;
    if (framebuffer) {
        g_limine_boot.framebuffer_base =
            limine_physical((u64)(uintptr_t)framebuffer->address, hhdm);
        g_limine_boot.framebuffer_size = framebuffer->pitch * framebuffer->height;
        g_limine_boot.width = (u32)framebuffer->width;
        g_limine_boot.height = (u32)framebuffer->height;
        g_limine_boot.pitch = (u32)framebuffer->pitch;
        /* render.c's format 0 means "swap R/B before storing" and format 1
         * means the native u32 already matches memory.  A framebuffer with
         * red above blue (the usual BGRX layout) therefore needs no swap;
         * the old >= comparison made blue render as orange. */
        g_limine_boot.pixel_format =
            framebuffer->red_mask_shift > framebuffer->blue_mask_shift ? 1 : 0;
    }

    if (g_limine_rsdp.response && g_limine_rsdp.response->address)
        g_limine_boot.rsdp = limine_physical(
            (u64)(uintptr_t)g_limine_rsdp.response->address, hhdm);

    build_memory_map(g_limine_memmap.response, hhdm);

    struct limine_file *flat = find_module("KERNEL.BIN");
    if (flat) {
        g_limine_boot.boot_image_phys =
            limine_physical((u64)(uintptr_t)flat->address, hhdm);
        g_limine_boot.boot_image_size = flat->size;
    }
    struct limine_file *font = find_module("MiSans.ttf");
    if (!font)
        font = find_module("MiSansGlobal-Regular.ttf");
    if (!font)
        font = find_module("Inter-Regular.otf");
    if (font) {
        g_limine_boot.font_phys =
            limine_physical((u64)(uintptr_t)font->address, hhdm);
        g_limine_boot.font_size = font->size;
    }
    struct limine_file *wallpaper = find_module("WALL.RGB");
    if (wallpaper) {
        g_limine_boot.wallpaper_phys =
            limine_physical((u64)(uintptr_t)wallpaper->address, hhdm);
        g_limine_boot.wallpaper_size = wallpaper->size;
    }

    if (install_identity_map(hhdm, physical_base, virtual_base) != 0)
        return 0;
    g_limine_boot.page_table_root =
        kernel_physical((u64)(uintptr_t)g_limine_pml4, physical_base,
                        virtual_base);
    return &g_limine_boot;
}
