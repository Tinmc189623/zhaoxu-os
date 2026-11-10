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
/* Rule 25: BOOTX64.EFI must not embed the kernel.  The loader reads
 * \EFI\BOOT\KERNEL.BIN from the boot volume instead.  The staging area is
 * 16 MiB, with the memory map at +12 MiB and boot_info at +14 MiB. */
#define KERNEL_FILE_MAX  0x00B00000ULL

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
    UINT64 font_phys;
    UINT64 font_size;
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

struct elf64_ehdr {
    UINT8 ident[16];
    UINT16 type;
    UINT16 machine;
    UINT32 version;
    UINT64 entry;
    UINT64 phoff;
    UINT64 shoff;
    UINT32 flags;
    UINT16 ehsize;
    UINT16 phentsize;
    UINT16 phnum;
    UINT16 shentsize;
    UINT16 shnum;
    UINT16 shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    UINT32 type;
    UINT32 flags;
    UINT64 offset;
    UINT64 vaddr;
    UINT64 paddr;
    UINT64 filesz;
    UINT64 memsz;
    UINT64 align;
} __attribute__((packed));

#define ELF_PT_LOAD 1

static UINT64 load_kernel_from_iso(EFI_HANDLE image_handle, UINT64 staging);

static UINT64 load_elf_image(const void *buffer, UINT64 file_size,
                             UINT64 staging)
{
    const struct elf64_ehdr *ehdr = (const struct elf64_ehdr *)buffer;
    if (!file_size || file_size < sizeof(*ehdr) ||
        ehdr->ident[0] != 0x7F || ehdr->ident[1] != 'E' ||
        ehdr->ident[2] != 'L' || ehdr->ident[3] != 'F' ||
        ehdr->ident[4] != 2 || ehdr->type != 2 || ehdr->machine != 62 ||
        ehdr->phentsize < sizeof(struct elf64_phdr) || !ehdr->phnum ||
        ehdr->entry != KERNEL_VMA)
        return 0;

    UINT64 span = 0;
    for (UINT16 i = 0; i < ehdr->phnum; i++) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)
            ((UINT64)(UINTN)buffer + ehdr->phoff +
             (UINT64)i * ehdr->phentsize);
        if (ph->type != ELF_PT_LOAD || ph->memsz == 0)
            continue;
        if (ph->vaddr < KERNEL_VMA || ph->filesz > ph->memsz ||
            ph->offset + ph->filesz > file_size)
            continue;
        UINT64 offset = ph->vaddr - KERNEL_VMA;
        if (offset + ph->memsz > KERNEL_MAP_SIZE)
            continue;
        memcpy((void *)(UINTN)(staging + offset),
               (const void *)(UINTN)((UINT64)(UINTN)buffer + ph->offset),
               ph->filesz);
        memset((void *)(UINTN)(staging + offset + ph->filesz), 0,
               ph->memsz - ph->filesz);
        if (offset + ph->filesz > span)
            span = offset + ph->filesz;
    }
    return span;
}

static UINT64 load_kernel_elf(EFI_HANDLE image_handle, UINT64 staging)
{
    static const EFI_GUID loaded_guid = ZHAOXU_LOADED_IMAGE_GUID;
    static const EFI_GUID fs_guid = ZHAOXU_SIMPLE_FS_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *image = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *file = 0;
    EFI_STATUS status;
    void *buffer = 0;

    /* ISO boots take the kernel from the CD root, outside the El Torito ESP. */
    UINT64 span = load_kernel_from_iso(image_handle, staging);
    if (span)
        return span;

    status = g_bs->HandleProtocol(image_handle, &loaded_guid, (void **)&image);
    if (EFI_ERROR(status) || !image || !image->DeviceHandle)
        return 0;
    status = g_bs->HandleProtocol(image->DeviceHandle, &fs_guid,
                                  (void **)&fs);
    if (EFI_ERROR(status) || !fs)
        return 0;
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || !root)
        return 0;

    static CHAR16 esp_path[] = L"\\EFI\\BOOT\\KERNEL.ELF";
    static CHAR16 root_path[] = L"\\KERNEL.ELF";
    CHAR16 *paths[2] = { esp_path, root_path };
    for (int i = 0; i < 2 && !file; i++) {
        status = root->Open(root, &file, paths[i], EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(status))
            file = 0;
    }
    if (!file) {
        root->Close(root);
        return 0;
    }

    status = g_bs->AllocatePool(EFI_LOADER_DATA, KERNEL_FILE_MAX, &buffer);
    if (EFI_ERROR(status) || !buffer) {
        file->Close(file);
        root->Close(root);
        return 0;
    }

    UINT64 file_size = 0;
    for (;;) {
        if (file_size >= KERNEL_FILE_MAX)
            break;
        UINTN chunk = 0x100000;
        if (file_size + chunk > KERNEL_FILE_MAX)
            chunk = (UINTN)(KERNEL_FILE_MAX - file_size);
        status = file->Read(file, &chunk,
                            (void *)(UINTN)((UINT64)(UINTN)buffer + file_size));
        if (EFI_ERROR(status)) {
            file_size = 0;
            break;
        }
        if (chunk == 0)
            break;
        file_size += chunk;
    }
    file->Close(file);
    root->Close(root);

    span = load_elf_image(buffer, file_size, staging);
    g_bs->FreePool(buffer);
    return span;
}

#define FONT_FILE_MAX 0x00800000ULL

typedef EFI_STATUS (EFIAPI *LOCATE_HANDLE_BUFFER)(
    UINT32 SearchType, const EFI_GUID *Protocol, void *SearchKey,
    UINTN *NoHandles, EFI_HANDLE **Buffer);

#define EFI_BY_PROTOCOL 2

static UINT32 iso_le32(const UINT8 *p)
{
    return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) |
           ((UINT32)p[3] << 24);
}

static int iso_match(const UINT8 *data, const char *text, UINT8 length)
{
    for (UINT8 i = 0; i < length; i++) {
        if (data[i] != (UINT8)text[i])
            return 0;
    }
    return 1;
}

static int iso_name_equal(const UINT8 *name, UINT8 length, const char *want)
{
    UINT8 count = length;
    for (UINT8 i = 0; i < length; i++) {
        if (name[i] == ';') {
            count = i;
            break;
        }
    }
    while (count > 0 && (name[count - 1] == ' ' || name[count - 1] == '.'))
        count--;

    const char *w = want;
    for (UINT8 i = 0; i < count; i++) {
        char a = (char)name[i];
        char b = *w++;
        if (a >= 'a' && a <= 'z')
            a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z')
            b = (char)(b - 'a' + 'A');
        if (!b || a != b)
            return 0;
    }
    return *w == 0;
}

static int iso_read_file(EFI_BLOCK_IO_PROTOCOL *bio, UINT32 extent, UINT32 size,
                         void *destination)
{
    UINT32 block = bio->Media->BlockSize;
    if (!block)
        return 0;
    UINT64 byte = (UINT64)extent * 2048ULL;
    EFI_LBA lba = byte / block;
    UINT32 inner = (UINT32)(byte % block);
    UINT64 total = (UINT64)inner + size;
    UINT64 blocks = (total + block - 1) / block;
    UINT8 *buffer = 0;
    EFI_STATUS status = g_bs->AllocatePool(EFI_LOADER_DATA,
                                           (UINTN)(blocks * block),
                                           (void **)&buffer);
    if (EFI_ERROR(status) || !buffer)
        return 0;
    status = bio->ReadBlocks(bio, bio->Media->MediaId, lba,
                             (UINTN)(blocks * block), buffer);
    if (EFI_ERROR(status)) {
        g_bs->FreePool(buffer);
        return 0;
    }
    memcpy(destination, buffer + inner, size);
    g_bs->FreePool(buffer);
    return 1;
}

static UINT8 *iso_read_directory(EFI_BLOCK_IO_PROTOCOL *bio, UINT32 extent,
                                 UINT32 size)
{
    if (size == 0 || size > 0x100000)
        return 0;
    UINT8 *buffer = 0;
    EFI_STATUS status = g_bs->AllocatePool(EFI_LOADER_DATA, size,
                                           (void **)&buffer);
    if (EFI_ERROR(status) || !buffer)
        return 0;
    if (!iso_read_file(bio, extent, size, buffer)) {
        g_bs->FreePool(buffer);
        return 0;
    }
    return buffer;
}

static int iso_find(const UINT8 *directory, UINT32 size, const char *name,
                    UINT32 *extent, UINT32 *bytes, UINT8 *flags)
{
    UINT32 offset = 0;
    while (offset + 33 <= size) {
        UINT8 length = directory[offset];
        if (length < 33) {
            offset = (offset / 2048 + 1) * 2048;
            continue;
        }
        UINT8 name_length = directory[offset + 32];
        if (name_length && offset + 33 + name_length <= size &&
            iso_name_equal(directory + offset + 33, name_length, name)) {
            *extent = iso_le32(directory + offset + 2);
            *bytes = iso_le32(directory + offset + 10);
            *flags = directory[offset + 25];
            return 1;
        }
        offset += length;
    }
    return 0;
}

static int iso_is_iso(EFI_BLOCK_IO_PROTOCOL *bio)
{
    UINT8 pvd[2048];
    return iso_read_file(bio, 16, sizeof(pvd), pvd) &&
           pvd[0] == 1 && iso_match(pvd + 1, "CD001", 5);
}

/* The image device is the El Torito FAT volume, so probe it first and then
 * every Block I/O handle until one answers with an ISO9660 PVD. */
static EFI_BLOCK_IO_PROTOCOL *iso_device(EFI_HANDLE image_handle)
{
    static EFI_BLOCK_IO_PROTOCOL *cached;
    static const EFI_GUID loaded_guid = ZHAOXU_LOADED_IMAGE_GUID;
    static const EFI_GUID block_guid = ZHAOXU_BLOCK_IO_GUID;
    if (cached)
        return cached;

    EFI_LOADED_IMAGE_PROTOCOL *image = 0;
    if (!EFI_ERROR(g_bs->HandleProtocol(image_handle, &loaded_guid,
                                        (void **)&image)) &&
        image && image->DeviceHandle) {
        EFI_BLOCK_IO_PROTOCOL *bio = 0;
        if (!EFI_ERROR(g_bs->HandleProtocol(image->DeviceHandle, &block_guid,
                                            (void **)&bio)) &&
            bio && iso_is_iso(bio)) {
            cached = bio;
            return bio;
        }
    }

    LOCATE_HANDLE_BUFFER locate =
        (LOCATE_HANDLE_BUFFER)g_bs->LocateHandleBuffer;
    EFI_HANDLE *handles = 0;
    UINTN count = 0;
    if (EFI_ERROR(locate(EFI_BY_PROTOCOL, &block_guid, 0, &count, &handles)) ||
        !handles)
        return 0;
    for (UINTN i = 0; i < count && !cached; i++) {
        EFI_BLOCK_IO_PROTOCOL *bio = 0;
        if (!EFI_ERROR(g_bs->HandleProtocol(handles[i], &block_guid,
                                            (void **)&bio)) &&
            bio && iso_is_iso(bio))
            cached = bio;
    }
    g_bs->FreePool(handles);
    return cached;
}

/* Resolve "DIR/FILE.EXT" inside the ISO9660 root directory. */
static int iso_find_path(EFI_BLOCK_IO_PROTOCOL *bio, const char *path,
                         UINT32 *out_extent, UINT32 *out_bytes)
{
    UINT8 pvd[2048];
    if (!iso_read_file(bio, 16, sizeof(pvd), pvd))
        return 0;
    if (pvd[0] != 1 || !iso_match(pvd + 1, "CD001", 5))
        return 0;

    UINT32 dir_extent = iso_le32(pvd + 158);
    UINT32 dir_size = iso_le32(pvd + 166);
    UINT8 *dir = iso_read_directory(bio, dir_extent, dir_size);
    if (!dir)
        return 0;

    int found = 0;
    while (*path && dir) {
        char name[32];
        UINT8 length = 0;
        while (*path && *path != '/' && length + 1 < sizeof(name))
            name[length++] = *path++;
        name[length] = 0;
        while (*path == '/')
            path++;

        UINT32 extent = 0;
        UINT32 bytes = 0;
        UINT8 flags = 0;
        found = iso_find(dir, dir_size, name, &extent, &bytes, &flags);
        g_bs->FreePool(dir);
        dir = 0;
        if (!found)
            return 0;

        if (*path) {
            if (!(flags & 2))
                return 0;
            dir_extent = extent;
            dir_size = bytes;
            dir = iso_read_directory(bio, dir_extent, dir_size);
        } else {
            *out_extent = extent;
            *out_bytes = bytes;
            return !(flags & 2);
        }
    }
    if (dir)
        g_bs->FreePool(dir);
    return 0;
}

static UINT64 load_kernel_from_iso(EFI_HANDLE image_handle, UINT64 staging)
{
    EFI_BLOCK_IO_PROTOCOL *bio = iso_device(image_handle);
    if (!bio)
        return 0;

    UINT32 extent = 0;
    UINT32 bytes = 0;
    if (!iso_find_path(bio, "INSTALL/KERNEL.ELF", &extent, &bytes) ||
        bytes < sizeof(struct elf64_ehdr) || bytes > KERNEL_FILE_MAX)
        return 0;

    void *buffer = 0;
    if (EFI_ERROR(g_bs->AllocatePool(EFI_LOADER_DATA, bytes, &buffer)) ||
        !buffer)
        return 0;
    if (!iso_read_file(bio, extent, bytes, buffer)) {
        g_bs->FreePool(buffer);
        return 0;
    }

    UINT64 span = load_elf_image(buffer, bytes, staging);
    g_bs->FreePool(buffer);
    if (span)
        boot_print("Kernel: /install/KERNEL.ELF from the ISO.\r\n");
    return span;
}

static void load_boot_font(EFI_HANDLE image_handle, UINT64 *physical,
                           UINT64 *bytes)
{
    *physical = 0;
    *bytes = 0;

    EFI_BLOCK_IO_PROTOCOL *bio = iso_device(image_handle);
    if (!bio)
        return;

    UINT32 extent = 0;
    UINT32 size = 0;
    if (!iso_find_path(bio, "INSTALL/WQY.TTC", &extent, &size) ||
        size < 13 || size > FONT_FILE_MAX)
        return;

    UINT64 pages = (size + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS address = 0xFFFFFFFFULL;
    EFI_STATUS status = g_bs->AllocatePages(EFI_ALLOCATE_MAX_ADDRESS,
                                            EFI_LOADER_DATA, pages, &address);
    if (EFI_ERROR(status) || !address)
        status = g_bs->AllocatePages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA,
                                     pages, &address);
    if (EFI_ERROR(status) || !address)
        return;

    if (!iso_read_file(bio, extent, size, (void *)(UINTN)address))
        return;
    *physical = (UINT64)address;
    *bytes = size;
    boot_print("Font: /install/WQY.TTC from the ISO.\r\n");
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

    boot_print("Loading \\EFI\\BOOT\\KERNEL.ELF...\r\n");
    memset((void *)kernel_phys, 0, KERNEL_MAP_SIZE);
    UINT64 kernel_size = load_kernel_elf(image_handle, kernel_phys);
    if (!kernel_size) {
        boot_print("Cannot load KERNEL.ELF from the boot volume.\r\n");
        halt();
    }

    UINT64 font_phys = 0;
    UINT64 font_size = 0;
    load_boot_font(image_handle, &font_phys, &font_size);

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
    bi->kernel_size = kernel_size;
    bi->kernel_map_size = KERNEL_MAP_SIZE;
    bi->page_table_root = pt_alloc;
    bi->flags = 1;
    bi->font_phys = font_phys;
    bi->font_size = font_size;

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
