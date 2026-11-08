/* Zhaoxu OS - ZhaoxuBootloader, UEFI x86_64 entry (C)
 *
 * Selects the best GOP mode (1920x1080 preferred), loads the flat kernel
 * image, builds identity + higher-half page tables, writes the boot_info
 * contract at +14 MiB and hands off to the Yuanshuo kernel at -2 GiB.
 */

#include "uefi.h"

#ifndef HEADLESS
#define HEADLESS 0
#endif

#define KERNEL_VMA       0xffffffff80000000ULL
#define KERNEL_MAP_SIZE  0x01000000ULL
#define KERNEL_STACK_TOP 0xffffffff80f00000ULL
#define BOOT_INFO_OFF    0x00e00000ULL
#define MMAP_OFF         0x00c00000ULL
#define MMAP_MAX         0x00100000ULL
#define KERNEL_ALLOC     0x01200000ULL
#define PAGE_TABLE_PAGES (32ULL)
#define BOOT_MAGIC       0x5A4F5331u

extern const UINT8 kernel_blob[];
extern const UINT8 kernel_blob_end[];

static EFI_SYSTEM_TABLE *g_st;
static EFI_BOOT_SERVICES *g_bs;

static UINT64 g_fb_base;
static UINT64 g_fb_size;
static UINT32 g_fb_width;
static UINT32 g_fb_height;
static UINT32 g_fb_pitch;
static UINT32 g_fb_format;

struct boot_info_blob {
    UINT64 magic;
    UINT64 framebuffer_base;
    UINT64 framebuffer_size;
    UINT32 width;
    UINT32 height;
    UINT32 pitch;
    UINT32 pixel_format;
    UINT64 memory_map;
    UINT64 memory_map_size;
    UINT64 memory_map_desc_size;
    UINT32 memory_map_desc_version;
    UINT32 reserved0;
    UINT64 rsdp;
    UINT64 kernel_phys_base;
    UINT64 kernel_virt_base;
    UINT64 kernel_size;
    UINT64 kernel_map_size;
    UINT64 page_table_root;
    UINT64 flags;
} __attribute__((packed));

/* no libc: clang is built with -fno-builtin so these loops stay loops */
void *memset(void *dst, int v, UINTN n)
{
    UINT8 *d = (UINT8 *)dst;
    while (n--)
        *d++ = (UINT8)v;
    return dst;
}

void *memcpy(void *dst, const void *src, UINTN n)
{
    UINT8 *d = (UINT8 *)dst;
    const UINT8 *s = (const UINT8 *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b)
{
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3)
        return 0;
    for (int i = 0; i < 8; i++)
        if (a->Data4[i] != b->Data4[i])
            return 0;
    return 1;
}

static void serial_char(char c)
{
    for (UINT32 spin = 0; spin < 100000; spin++) {
        UINT8 lsr;
        __asm__ volatile("inb %1, %0" : "=a"(lsr) : "Nd"((UINT16)0x3FD));
        if (lsr & 0x20)
            break;
    }
    __asm__ volatile("outb %0, %1" ::"a"(c), "Nd"((UINT16)0x3F8));
}

static void serial_str(const char *s)
{
    while (*s)
        serial_char(*s++);
}

static void serial_u32(UINT32 v)
{
    char buf[11];
    int i = 0;
    if (!v) {
        serial_char('0');
        return;
    }
    while (v) {
        buf[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i)
        serial_char(buf[--i]);
}

#if !HEADLESS
static void console_str(const char *s)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out;
    CHAR16 buf[2];
    if (!g_st || !(out = g_st->ConOut) || !out->OutputString)
        return;
    while (*s) {
        buf[0] = (CHAR16)(UINT8)*s++;
        buf[1] = 0;
        out->OutputString(out, buf);
    }
}
#else
static void console_str(const char *s)
{
    (void)s;
}
#endif

static void boot_print(const char *s)
{
    serial_str(s);
    console_str(s);
}

static __attribute__((noreturn)) void halt(void)
{
    __asm__ volatile("cli");
    for (;;)
        __asm__ volatile("hlt");
}

#if !HEADLESS
static int setup_gop(void)
{
    static const EFI_GUID gop_guid = ZHAOXU_GOP_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status;

    status = g_bs->LocateProtocol(&gop_guid, 0, (void **)&gop);
    if (EFI_ERROR(status) || !gop || !gop->Mode)
        return -1;

    UINT32 best_mode = gop->Mode->Mode;
    UINT64 best_score = 0;
    UINT32 max_mode = gop->Mode->MaxMode;
    for (UINT32 i = 0; i < max_mode; i++) {
        UINTN info_size = 0;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
        status = gop->QueryMode(gop, i, &info_size, &info);
        if (EFI_ERROR(status) || !info)
            continue;
        if (info->PixelFormat == PixelBltOnly) {
            g_bs->FreePool(info);
            continue;
        }
        UINT64 score =
            (UINT64)info->HorizontalResolution * info->VerticalResolution;
        if (info->HorizontalResolution == 1920 &&
            info->VerticalResolution == 1080)
            score += 1ULL << 40;
        if (score > best_score) {
            best_score = score;
            best_mode = i;
        }
        g_bs->FreePool(info);
    }

    if (best_score && best_mode != gop->Mode->Mode)
        gop->SetMode(gop, best_mode);

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = gop->Mode->Info;
    if (!mi)
        return -1;

    g_fb_base = gop->Mode->FrameBufferBase;
    g_fb_size = gop->Mode->FrameBufferSize;
    g_fb_width = mi->HorizontalResolution;
    g_fb_height = mi->VerticalResolution;
    g_fb_pitch = mi->PixelsPerScanLine * 4;
    g_fb_format = (UINT32)mi->PixelFormat;
    if (!g_fb_size)
        g_fb_size = (UINT64)g_fb_pitch * g_fb_height;
    return 0;
}
#endif

static UINT64 find_rsdp(void)
{
    static const EFI_GUID acpi20_guid = ZHAOXU_ACPI20_GUID;
    static const EFI_GUID acpi10_guid = ZHAOXU_ACPI10_GUID;
    UINT64 rsdp20 = 0, rsdp10 = 0;
    EFI_CONFIGURATION_TABLE *ct = g_st->ConfigurationTable;
    UINTN count = g_st->NumberOfTableEntries;

    for (UINTN i = 0; i < count; i++) {
        if (!rsdp20 && guid_equal(&ct[i].VendorGuid, &acpi20_guid))
            rsdp20 = (UINT64)(UINTN)ct[i].VendorTable;
        if (!rsdp10 && guid_equal(&ct[i].VendorGuid, &acpi10_guid))
            rsdp10 = (UINT64)(UINTN)ct[i].VendorTable;
    }
    return rsdp20 ? rsdp20 : rsdp10;
}

static void build_page_tables(UINT64 pt_phys, UINT64 kernel_phys)
{
    UINT64 *pt = (UINT64 *)pt_phys;
    UINT64 *pml4 = pt;              /* +0x0000 */
    UINT64 *low_pdpt = pt + 512;    /* +0x1000 */
    UINT64 *high_pdpt = pt + 512 * 6; /* +0x6000 */
    UINT64 *kernel_pd = pt + 512 * 7; /* +0x7000 */

    memset(pt, 0, PAGE_TABLE_PAGES * 4096);

    pml4[0] = (UINT64)(UINTN)low_pdpt | 3;
    pml4[511] = (UINT64)(UINTN)high_pdpt | 3;

    for (int i = 0; i < 4; i++) {
        UINT64 *pd = pt + 512 * (2 + i);
        low_pdpt[i] = (UINT64)(UINTN)pd | 3;
        for (int j = 0; j < 512; j++)
            pd[j] = (((UINT64)i << 30) + ((UINT64)j << 21)) | 0x83;
    }

    high_pdpt[510] = (UINT64)(UINTN)kernel_pd | 3;
    for (UINT64 k = 0; k < KERNEL_MAP_SIZE / 0x200000ULL; k++)
        kernel_pd[k] = (kernel_phys + (k << 21)) | 0x83;
}

/* Pages 8..PAGE_TABLE_PAGES-1 are reserved for mappings that must survive
 * the CR3 switch below.  Some firmware (VMware) loads EFI images above
 * 4 GiB, GOP framebuffers commonly sit above 4 GiB with large VRAM, and
 * the firmware stack may too; all of them must stay reachable. */
static UINT64 *g_pml4;
static UINT64 g_pt_pool;
static UINT64 g_pt_end;
static int g_pt_full;

/* Take one page from the reserved page-table pool. */
static UINT64 *pt_alloc_page(void)
{
    if (g_pt_pool + 4096 > g_pt_end) {
        g_pt_full = 1;
        return 0;
    }
    UINT64 *page = (UINT64 *)(UINTN)g_pt_pool;
    g_pt_pool += 4096;
    memset(page, 0, 4096);
    return page;
}

static void map_identity_2m(UINT64 addr)
{
    addr &= ~0x1FFFFFULL;
    UINT64 pml4i = (addr >> 39) & 511;
    UINT64 pdpti = (addr >> 30) & 511;
    UINT64 pdi   = (addr >> 21) & 511;

    UINT64 e = g_pml4[pml4i];
    if (!(e & 1)) {
        UINT64 *pdpt = pt_alloc_page();
        if (!pdpt)
            return;
        g_pml4[pml4i] = (UINT64)(UINTN)pdpt | 3;
        e = g_pml4[pml4i];
    }
    UINT64 *pdpt = (UINT64 *)(UINTN)(e & 0x000FFFFFFFFFF000ULL);
    e = pdpt[pdpti];
    if (!(e & 1)) {
        UINT64 *pd = pt_alloc_page();
        if (!pd)
            return;
        pdpt[pdpti] = (UINT64)(UINTN)pd | 3;
        e = pdpt[pdpti];
    }
    UINT64 *pd = (UINT64 *)(UINTN)(e & 0x000FFFFFFFFFF000ULL);
    pd[pdi] = addr | 0x83;
}

static void map_identity_range(UINT64 base, UINT64 size)
{
    UINT64 end = base + size;
    for (UINT64 a = base & ~0x1FFFFFULL; a < end; a += 0x200000ULL)
        map_identity_2m(a);
}

/* Boot structures are written before the switch to our own page tables, so
 * firmware must hand out pages inside the identity-mapped low 4 GiB.  A
 * plain ANY_PAGES allocation can land above 4 GiB on large-memory systems
 * and the first memset would then triple fault. */
static EFI_STATUS alloc_below_4g(UINTN pages, EFI_PHYSICAL_ADDRESS *out)
{
    *out = 0xC0000000ULL; /* keep clear of the 32-bit PCI MMIO hole */
    EFI_STATUS status = g_bs->AllocatePages(EFI_ALLOCATE_MAX_ADDRESS,
                                            EFI_LOADER_DATA, pages, out);
    if (!EFI_ERROR(status) && *out &&
        *out + (UINT64)pages * 4096ULL <= 0x100000000ULL)
        return status;

    *out = 0xF0000000ULL;
    status = g_bs->AllocatePages(EFI_ALLOCATE_MAX_ADDRESS,
                                 EFI_LOADER_DATA, pages, out);
    if (!EFI_ERROR(status) && *out &&
        *out + (UINT64)pages * 4096ULL <= 0x100000000ULL)
        return status;
    return EFI_BUFFER_TOO_SMALL;
}

EFI_STATUS EFIAPI efi_start(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    g_st = st;
    g_bs = st->BootServices;

    boot_print("\r\nZhaoxuBootloader (C, UEFI x86_64)\r\n");
    boot_print("Probing UEFI services...\r\n");

#if !HEADLESS
    if (setup_gop() == 0) {
        boot_print("GOP framebuffer acquired: ");
        serial_u32(g_fb_width);
        serial_char('x');
        serial_u32(g_fb_height);
        serial_str(" pitch ");
        serial_u32(g_fb_pitch);
        serial_str("\r\n");
    } else {
        boot_print("No GOP framebuffer; serial console only.\r\n");
    }
#endif

    UINT64 rsdp = find_rsdp();

    /* Prefer a fixed low address: the kernel and its memory map stay below
     * 4 GiB where the kernel's identity mapping always reaches them. */
    EFI_PHYSICAL_ADDRESS kernel_alloc = 0x01000000ULL;
    EFI_STATUS status = g_bs->AllocatePages(EFI_ALLOCATE_ADDRESS,
                                            EFI_LOADER_DATA,
                                            KERNEL_ALLOC / 4096,
                                            &kernel_alloc);
    if (EFI_ERROR(status) || !kernel_alloc) {
        status = alloc_below_4g(KERNEL_ALLOC / 4096, &kernel_alloc);
    }
    if (EFI_ERROR(status) || !kernel_alloc ||
        kernel_alloc + KERNEL_ALLOC > 0x100000000ULL) {
        boot_print("AllocatePages for kernel failed.\r\n");
        halt();
    }
    UINT64 kernel_phys = (kernel_alloc + 0x1FFFFFULL) & ~0x1FFFFFULL;
    UINT64 blob_size = (UINT64)(kernel_blob_end - kernel_blob);

    boot_print("Loading Yuanshuo kernel...\r\n");
    memset((void *)kernel_phys, 0, KERNEL_MAP_SIZE);
    memcpy((void *)kernel_phys, kernel_blob, blob_size);

    EFI_PHYSICAL_ADDRESS pt_alloc = 0;
    status = alloc_below_4g(PAGE_TABLE_PAGES, &pt_alloc);
    if (EFI_ERROR(status) || !pt_alloc ||
        pt_alloc + PAGE_TABLE_PAGES * 4096ULL > 0x100000000ULL) {
        boot_print("AllocatePages for page tables failed.\r\n");
        halt();
    }
    build_page_tables(pt_alloc, kernel_phys);

    /* Keep executing across the CR3 switch: identity-map this EFI image
     * (firmware may have loaded it above 4 GiB) plus the whole kernel
     * staging area (kernel image, boot_info and UEFI memory map). */
    g_pml4 = (UINT64 *)pt_alloc;
    g_pt_pool = pt_alloc + 8 * 4096;
    g_pt_end = pt_alloc + PAGE_TABLE_PAGES * 4096;
    static const EFI_GUID loaded_image_guid = ZHAOXU_LOADED_IMAGE_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    if (!EFI_ERROR(g_bs->HandleProtocol(image_handle, &loaded_image_guid,
                                        (void **)&li)) && li) {
        map_identity_range((UINT64)(UINTN)li->ImageBase, li->ImageSize);
    }
    map_identity_range(kernel_phys, KERNEL_MAP_SIZE);
    /* GOP framebuffers with large VRAM live above 4 GiB on real firmware;
     * the kernel writes pixels through this identity mapping long before
     * it builds page tables of its own. */
    if (g_fb_base && g_fb_size)
        map_identity_range(g_fb_base, g_fb_size);
    /* The firmware stack is still live until the far jump to the kernel. */
    UINT64 stack_now;
    __asm__ volatile("mov %%rsp, %0" : "=r"(stack_now));
    map_identity_range(stack_now & ~0xFFFULL, 0x4000);
    if (g_pt_full) {
        boot_print("Page-table pool exhausted.\r\n");
        halt();
    }

    struct boot_info_blob *bi =
        (struct boot_info_blob *)(kernel_phys + BOOT_INFO_OFF);
    bi->magic = BOOT_MAGIC;
    bi->framebuffer_base = g_fb_base;
    bi->framebuffer_size = g_fb_size;
    bi->width = g_fb_width;
    bi->height = g_fb_height;
    bi->pitch = g_fb_pitch;
    bi->pixel_format = g_fb_format;
    bi->memory_map = KERNEL_VMA + MMAP_OFF;
    bi->rsdp = rsdp;
    bi->kernel_phys_base = kernel_phys;
    bi->kernel_virt_base = KERNEL_VMA;
    bi->kernel_size = blob_size;
    bi->kernel_map_size = KERNEL_MAP_SIZE;
    bi->page_table_root = pt_alloc;
    bi->flags = 1;

    UINTN mmap_size = MMAP_MAX;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_ver = 0;
    int ok = 0;
    for (int tries = 0; tries < 8; tries++) {
        mmap_size = MMAP_MAX;
        status = g_bs->GetMemoryMap(
            &mmap_size, (EFI_MEMORY_DESCRIPTOR *)(kernel_phys + MMAP_OFF),
            &map_key, &desc_size, &desc_ver);
        if (!EFI_ERROR(status)) {
            ok = 1;
            break;
        }
        if (status != EFI_BUFFER_TOO_SMALL)
            break;
    }
    if (!ok) {
        boot_print("GetMemoryMap failed.\r\n");
        halt();
    }
    bi->memory_map_size = mmap_size;
    bi->memory_map_desc_size = desc_size;
    bi->memory_map_desc_version = desc_ver;

    /* No further ConOut calls: firmware may allocate while printing and
     * invalidate the memory-map key required by ExitBootServices. */
    serial_str("Memory map acquired.\r\n");
    serial_str("Exiting UEFI boot services...\r\n");

    ok = 0;
    for (int tries = 0; tries < 5; tries++) {
        status = g_bs->ExitBootServices(image_handle, map_key);
        if (!EFI_ERROR(status)) {
            ok = 1;
            break;
        }
        mmap_size = MMAP_MAX;
        status = g_bs->GetMemoryMap(
            &mmap_size, (EFI_MEMORY_DESCRIPTOR *)(kernel_phys + MMAP_OFF),
            &map_key, &desc_size, &desc_ver);
        if (!EFI_ERROR(status)) {
            bi->memory_map_size = mmap_size;
            bi->memory_map_desc_size = desc_size;
            bi->memory_map_desc_version = desc_ver;
        }
    }
    if (!ok) {
        serial_str("ExitBootServices failed.\r\n");
        halt();
    }

    __asm__ volatile("cli");
    serial_str("Handing off to Yuanshuo kernel.\r\n");
    __asm__ volatile("mov %0, %%cr3" ::"r"((UINT64)pt_alloc) : "memory");
    __asm__ volatile("mov %1, %%rsp\n\t"
                     "xor %%ebp, %%ebp\n\t"
                     "jmp *%2\n\t" ::"D"((UINT64)(KERNEL_VMA + BOOT_INFO_OFF)),
                     "r"((UINT64)KERNEL_STACK_TOP), "r"((UINT64)KERNEL_VMA)
                     : "memory");
    halt();
}
