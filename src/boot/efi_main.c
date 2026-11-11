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

static UINTN boot_strlen(const char *text)
{
    UINTN length = 0;
    while (text[length])
        length++;
    return length;
}

static int boot_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (UINT8)*a - (UINT8)*b;
}

static int boot_memcmp(const void *a, const void *b, UINTN n)
{
    const UINT8 *x = (const UINT8 *)a;
    const UINT8 *y = (const UINT8 *)b;
    for (UINTN i = 0; i < n; i++) {
        if (x[i] != y[i])
            return x[i] - y[i];
    }
    return 0;
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
static UINT64 load_kernel_from_ext4(EFI_HANDLE image_handle, UINT64 staging);
static UINT64 load_font_from_ext4(EFI_HANDLE image_handle,
                                  UINT64 *physical);

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

    /* Live ISO boots take the kernel from the CD root even when an installed
     * ext4 target is attached; installed disks fall back to ext4. */
    UINT64 span = load_kernel_from_iso(image_handle, staging);
    if (span)
        return span;
    span = load_kernel_from_ext4(image_handle, staging);
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

/* ---- minimal ext4 reader for installed systems ------------------- */

struct ext4_geometry {
    UINT32 block_size;
    UINT32 inode_size;
    UINT32 inodes_per_group;
    UINT32 blocks_per_group;
    UINT32 blocks_count;
    UINT32 groups;
    UINT32 desc_size;
    UINT32 gdt_block;
    UINT32 first_data_block;
    char label[17];
};

static struct ext4_geometry g_ext4g;
static EFI_BLOCK_IO_PROTOCOL *g_ext4d_bio;
static UINT64 g_ext4d_base;
static int g_ext4d_valid;
static int g_ext4d_tried;

static UINT16 ex_le16(const UINT8 *p)
{
    return (UINT16)(p[0] | ((UINT16)p[1] << 8));
}

static UINT32 ex_le32(const UINT8 *p)
{
    return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) |
           ((UINT32)p[3] << 24);
}

static UINT64 ex_le64(const UINT8 *p)
{
    return (UINT64)ex_le32(p) | ((UINT64)ex_le32(p + 4) << 32);
}

static int ext4_read(EFI_BLOCK_IO_PROTOCOL *bio, UINT64 base, UINT64 offset,
                     void *destination, UINTN length)
{
    if (!bio || !bio->Media || !bio->Media->BlockSize)
        return 0;
    UINTN media = bio->Media->BlockSize;
    UINT64 bytes = base * 512ULL + offset;
    UINT64 lba = bytes / media;
    UINTN inner = (UINTN)(bytes % media);
    UINTN total = inner + length;
    UINTN blocks = (total + media - 1) / media;
    void *buffer = 0;
    if (EFI_ERROR(g_bs->AllocatePool(EFI_LOADER_DATA, blocks * media,
                                     &buffer)) || !buffer)
        return 0;
    if (EFI_ERROR(bio->ReadBlocks(bio, bio->Media->MediaId, lba,
                                  blocks * media, buffer))) {
        g_bs->FreePool(buffer);
        return 0;
    }
    memcpy(destination, (UINT8 *)buffer + inner, length);
    g_bs->FreePool(buffer);
    return 1;
}

static int ext4_probe_at(EFI_BLOCK_IO_PROTOCOL *bio, UINT64 base, int *score)
{
    UINT8 sb[1024];
    if (!ext4_read(bio, base, 1024, sb, sizeof(sb)))
        return 0;
    if (ex_le16(sb + 0x38) != 0xEF53)
        return 0;
    UINT32 log = ex_le32(sb + 0x18);
    if (log > 2)
        return 0;
    UINT32 incompat = ex_le32(sb + 0x60);
    if (!(incompat & 0x40))
        return 0;

    struct ext4_geometry g;
    memset(&g, 0, sizeof(g));
    g.block_size = 1024u << log;
    g.inode_size = ex_le32(sb + 0x4C) >= 1 ? ex_le16(sb + 0x58) : 128;
    if (g.inode_size < 128 || g.inode_size > 1024)
        return 0;
    g.inodes_per_group = ex_le32(sb + 0x28);
    g.blocks_per_group = ex_le32(sb + 0x20);
    g.blocks_count = ex_le32(sb + 0x04);
    g.first_data_block = ex_le32(sb + 0x14);
    if (!g.inodes_per_group || !g.blocks_per_group || !g.blocks_count)
        return 0;
    g.desc_size = (incompat & 0x80) ? ex_le16(sb + 0xFE) : 32;
    if (g.desc_size < 32)
        g.desc_size = 32;
    if (g.desc_size > 64)
        g.desc_size = 64;
    g.gdt_block = g.first_data_block + 1;
    UINT32 data_blocks = g.blocks_count > g.first_data_block
                             ? g.blocks_count - g.first_data_block
                             : 0;
    g.groups = (data_blocks + g.blocks_per_group - 1) / g.blocks_per_group;
    if (!g.groups)
        return 0;
    for (int i = 0; i < 16 && sb[0x78 + i]; i++)
        g.label[i] = (char)sb[0x78 + i];
    g.label[16] = 0;

    *score = boot_strcmp(g.label, "ZHAOXUROOT") == 0 ? 2 : 1;
    g_ext4g = g;
    return 1;
}

static void ext4_scan_handle(EFI_BLOCK_IO_PROTOCOL *bio, int *best,
                             EFI_BLOCK_IO_PROTOCOL **best_bio,
                             UINT64 *best_base, int *best_score)
{
    if (!bio || !bio->Media)
        return;
    int score = 0;
    if (ext4_probe_at(bio, 0, &score)) {
        if (score > *best_score) {
            *best_score = score;
            *best_bio = bio;
            *best_base = 0;
            *best = 1;
            return;
        }
    }

    UINT8 sector[512];
    if (!ext4_read(bio, 0, 0, sector, sizeof(sector)))
        return;
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return;

    if (sector[446 + 4] == 0xEE) {
        UINT8 header[512];
        if (!ext4_read(bio, 0, 512, header, sizeof(header)) ||
            boot_memcmp(header, "EFI PART", 8) != 0)
            return;
        UINT64 entries_lba = ex_le64(header + 72);
        UINT32 entries = ex_le32(header + 80);
        UINT32 entry_size = ex_le32(header + 84);
        if (entry_size < 128 || entries > 128)
            return;
        for (UINT32 i = 0; i < entries; i++) {
            UINT8 entry[128];
            if (!ext4_read(bio, 0,
                           entries_lba * 512ULL + (UINT64)i * entry_size,
                           entry, sizeof(entry)))
                continue;
            int used = 0;
            for (int k = 0; k < 16; k++)
                used |= entry[k];
            if (!used)
                continue;
            UINT64 start = ex_le64(entry + 32);
            if (!start)
                continue;
            if (ext4_probe_at(bio, start, &score) && score > *best_score) {
                *best_score = score;
                *best_bio = bio;
                *best_base = start;
                *best = 1;
            }
        }
    } else {
        for (int i = 0; i < 4; i++) {
            const UINT8 *entry = sector + 446 + i * 16;
            UINT8 type = entry[4];
            UINT64 start = ex_le32(entry + 8);
            UINT32 count = ex_le32(entry + 12);
            if (!type || !count || !start)
                continue;
            if (ext4_probe_at(bio, start, &score) && score > *best_score) {
                *best_score = score;
                *best_bio = bio;
                *best_base = start;
                *best = 1;
            }
        }
    }
}

static int ext4_find_partition(EFI_HANDLE image_handle)
{
    if (g_ext4d_tried)
        return g_ext4d_valid;
    g_ext4d_tried = 1;

    static const EFI_GUID loaded_guid = ZHAOXU_LOADED_IMAGE_GUID;
    static const EFI_GUID block_guid = ZHAOXU_BLOCK_IO_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *image = 0;
    int best = 0;
    EFI_BLOCK_IO_PROTOCOL *best_bio = 0;
    UINT64 best_base = 0;
    int best_score = 0;

    if (!EFI_ERROR(g_bs->HandleProtocol(image_handle, &loaded_guid,
                                        (void **)&image)) &&
        image && image->DeviceHandle) {
        EFI_BLOCK_IO_PROTOCOL *bio = 0;
        if (!EFI_ERROR(g_bs->HandleProtocol(image->DeviceHandle, &block_guid,
                                            (void **)&bio)))
            ext4_scan_handle(bio, &best, &best_bio, &best_base,
                             &best_score);
    }

    LOCATE_HANDLE_BUFFER locate =
        (LOCATE_HANDLE_BUFFER)g_bs->LocateHandleBuffer;
    EFI_HANDLE *handles = 0;
    UINTN count = 0;
    if (!EFI_ERROR(locate(EFI_BY_PROTOCOL, &block_guid, 0, &count, &handles)) &&
        handles) {
        for (UINTN i = 0; i < count; i++) {
            EFI_BLOCK_IO_PROTOCOL *bio = 0;
            if (!EFI_ERROR(g_bs->HandleProtocol(handles[i], &block_guid,
                                                (void **)&bio)))
                ext4_scan_handle(bio, &best, &best_bio, &best_base,
                                 &best_score);
        }
        g_bs->FreePool(handles);
    }

    if (!best || !best_bio)
        return 0;
    g_ext4d_bio = best_bio;
    g_ext4d_base = best_base;
    g_ext4d_valid = 1;
    return 1;
}

static int ext4_read_inode(EFI_BLOCK_IO_PROTOCOL *bio, UINT64 base,
                           UINT32 ino, UINT8 *inode)
{
    if (ino == 0 || !g_ext4d_valid)
        return 0;
    UINT32 group = (ino - 1) / g_ext4g.inodes_per_group;
    UINT32 index = (ino - 1) % g_ext4g.inodes_per_group;
    if (group >= g_ext4g.groups)
        return 0;

    UINT8 desc[64];
    if (!ext4_read(bio, base,
                   (UINT64)g_ext4g.gdt_block * g_ext4g.block_size +
                       (UINT64)group * g_ext4g.desc_size,
                   desc, g_ext4g.desc_size))
        return 0;

    UINT64 table = ex_le32(desc + 8);
    if (g_ext4g.desc_size >= 64)
        table |= (UINT64)ex_le32(desc + 0x28) << 32;
    UINT32 bytes = g_ext4g.inode_size < 256 ? g_ext4g.inode_size : 256;
    memset(inode, 0, 256);
    return ext4_read(bio, base,
                     table * g_ext4g.block_size +
                         (UINT64)index * g_ext4g.inode_size,
                     inode, bytes);
}

static int ext4_map_node(EFI_BLOCK_IO_PROTOCOL *bio, UINT64 base,
                         const UINT8 *node, UINT16 depth, UINT32 logical,
                         UINT64 *physical, UINT32 *count)
{
    if (ex_le16(node) != 0xF30A)
        return 0;
    UINT16 entries = ex_le16(node + 2);
    if (entries > 1024)
        return 0;

    if (depth == 0) {
        for (UINT16 i = 0; i < entries; i++) {
            const UINT8 *entry = node + 12 + (UINT32)i * 12;
            UINT32 start = ex_le32(entry);
            UINT16 length = ex_le16(entry + 4);
            if (length > 0x8000)
                length = (UINT16)(length - 0x8000);
            if (!length || logical < start || logical >= start + length)
                continue;
            *physical = ((UINT64)ex_le16(entry + 6) << 32) |
                        ex_le32(entry + 8);
            *physical += logical - start;
            *count = start + length - logical;
            return 1;
        }
        return 0;
    }

    for (UINT16 i = 0; i < entries; i++) {
        const UINT8 *entry = node + 12 + (UINT32)i * 12;
        UINT32 start = ex_le32(entry);
        UINT32 next = i + 1 < entries ? ex_le32(entry + 12) : 0xFFFFFFFFu;
        if (logical < start || logical >= next)
            continue;
        UINT64 leaf = ((UINT64)ex_le16(entry + 8) << 32) | ex_le32(entry + 4);
        UINT8 block[4096];
        if (g_ext4g.block_size > sizeof(block))
            return 0;
        if (!ext4_read(bio, base, leaf * g_ext4g.block_size, block,
                       g_ext4g.block_size))
            return 0;
        return ext4_map_node(bio, base, block, (UINT16)(depth - 1), logical,
                             physical, count);
    }
    return 0;
}

static int ext4_map_inode(EFI_BLOCK_IO_PROTOCOL *bio, UINT64 base,
                          const UINT8 *inode, UINT32 logical,
                          UINT64 *physical, UINT32 *count)
{
    if (!(ex_le32(inode + 0x20) & 0x00080000))
        return 0;
    const UINT8 *node = inode + 0x28;
    if (ex_le16(node) != 0xF30A)
        return 0;
    return ext4_map_node(bio, base, node, ex_le16(node + 6), logical,
                         physical, count);
}

static UINT64 ext4_inode_size(const UINT8 *inode)
{
    return ex_le32(inode + 0x04);
}

static int ext4_read_inode_data(EFI_BLOCK_IO_PROTOCOL *bio, UINT64 base,
                                const UINT8 *inode, void *destination,
                                UINT64 length, UINT64 *read_out)
{
    UINT64 size = ext4_inode_size(inode);
    if (length > size)
        length = size;
    UINT8 block[4096];
    if (g_ext4g.block_size > sizeof(block))
        return 0;
    UINT8 *out = destination;
    UINT64 done = 0;
    while (done < length) {
        UINT32 logical = (UINT32)(done / g_ext4g.block_size);
        UINT32 within = (UINT32)(done % g_ext4g.block_size);
        UINT64 physical = 0;
        UINT32 count = 0;
        if (!ext4_map_inode(bio, base, inode, logical, &physical, &count))
            return 0;
        if (!ext4_read(bio, base, physical * g_ext4g.block_size, block,
                       g_ext4g.block_size))
            return 0;
        UINT64 chunk = length - done;
        if (chunk > g_ext4g.block_size - within)
            chunk = g_ext4g.block_size - within;
        memcpy(out + done, block + within, (UINTN)chunk);
        done += chunk;
    }
    if (read_out)
        *read_out = done;
    return 1;
}

static int ext4_dir_find(EFI_BLOCK_IO_PROTOCOL *bio, UINT64 base,
                         const UINT8 *inode, const char *name, UINT32 *ino)
{
    UINT64 size = ext4_inode_size(inode);
    UINT8 block[4096];
    if (g_ext4g.block_size > sizeof(block))
        return 0;
    UINT32 name_len = (UINT32)boot_strlen(name);

    for (UINT32 logical = 0;
         (UINT64)logical * g_ext4g.block_size < size; logical++) {
        UINT64 physical = 0;
        UINT32 count = 0;
        if (!ext4_map_inode(bio, base, inode, logical, &physical, &count))
            return 0;
        if (!ext4_read(bio, base, physical * g_ext4g.block_size, block,
                       g_ext4g.block_size))
            return 0;
        UINT32 offset = 0;
        while (offset + 8 <= g_ext4g.block_size) {
            const UINT8 *entry = block + offset;
            UINT32 child = ex_le32(entry);
            UINT16 rec_len = ex_le16(entry + 4);
            UINT8 child_len = entry[6];
            if (rec_len < 8 || offset + rec_len > g_ext4g.block_size)
                break;
            if (child && child_len == name_len &&
                boot_memcmp(entry + 8, name, name_len) == 0) {
                *ino = child;
                return 1;
            }
            offset += rec_len;
        }
    }
    return 0;
}

static int ext4_lookup(EFI_BLOCK_IO_PROTOCOL *bio, UINT64 base,
                       const char *path, UINT32 *ino)
{
    UINT32 current = 2;
    UINT8 inode[256];
    if (!ext4_read_inode(bio, base, current, inode))
        return 0;

    const char *cursor = path;
    while (*cursor == '/')
        cursor++;
    while (*cursor) {
        char component[256];
        UINT32 length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 >= sizeof(component))
                return 0;
            component[length++] = *cursor++;
        }
        component[length] = 0;
        while (*cursor == '/')
            cursor++;
        UINT32 next = 0;
        if (!ext4_dir_find(bio, base, inode, component, &next))
            return 0;
        current = next;
        if (!ext4_read_inode(bio, base, current, inode))
            return 0;
    }
    *ino = current;
    return 1;
}

static UINT64 load_kernel_from_ext4(EFI_HANDLE image_handle, UINT64 staging)
{
    if (!ext4_find_partition(image_handle))
        return 0;
    UINT32 ino = 0;
    if (!ext4_lookup(g_ext4d_bio, g_ext4d_base, "/KERNEL.ELF", &ino))
        return 0;
    UINT8 inode[256];
    if (!ext4_read_inode(g_ext4d_bio, g_ext4d_base, ino, inode))
        return 0;
    UINT64 size = ext4_inode_size(inode);
    if (size < sizeof(struct elf64_ehdr) || size > KERNEL_FILE_MAX)
        return 0;

    void *buffer = 0;
    if (EFI_ERROR(g_bs->AllocatePool(EFI_LOADER_DATA, (UINTN)size,
                                     &buffer)) || !buffer)
        return 0;
    UINT64 read = 0;
    if (!ext4_read_inode_data(g_ext4d_bio, g_ext4d_base, inode, buffer, size,
                              &read) ||
        read != size) {
        g_bs->FreePool(buffer);
        return 0;
    }
    UINT64 span = load_elf_image(buffer, size, staging);
    g_bs->FreePool(buffer);
    if (span)
        boot_print("Kernel: /KERNEL.ELF from ext4.\r\n");
    return span;
}

static UINT64 load_font_from_ext4(EFI_HANDLE image_handle, UINT64 *physical)
{
    *physical = 0;
    if (!ext4_find_partition(image_handle))
        return 0;
    UINT32 ino = 0;
    if (!ext4_lookup(g_ext4d_bio, g_ext4d_base, "/WQY.TTC", &ino))
        return 0;
    UINT8 inode[256];
    if (!ext4_read_inode(g_ext4d_bio, g_ext4d_base, ino, inode))
        return 0;
    UINT64 size = ext4_inode_size(inode);
    if (size < 13 || size > FONT_FILE_MAX)
        return 0;

    UINT64 pages = (size + 4095) / 4096;
    EFI_PHYSICAL_ADDRESS address = 0xFFFFFFFFULL;
    EFI_STATUS status = g_bs->AllocatePages(EFI_ALLOCATE_MAX_ADDRESS,
                                            EFI_LOADER_DATA, pages, &address);
    if (EFI_ERROR(status) || !address)
        status = g_bs->AllocatePages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA,
                                     pages, &address);
    if (EFI_ERROR(status) || !address)
        return 0;

    UINT64 read = 0;
    if (!ext4_read_inode_data(g_ext4d_bio, g_ext4d_base, inode,
                              (void *)(UINTN)address, size, &read) ||
        read != size)
        return 0;
    *physical = (UINT64)address;
    return size;
}

static void load_boot_font(EFI_HANDLE image_handle, UINT64 *physical,
                           UINT64 *bytes)
{
    *physical = 0;
    *bytes = 0;

    EFI_BLOCK_IO_PROTOCOL *bio = iso_device(image_handle);
    if (bio) {
        UINT32 extent = 0;
        UINT32 size = 0;
        if (iso_find_path(bio, "INSTALL/WQY.TTC", &extent, &size) &&
            size >= 13 && size <= FONT_FILE_MAX) {
            UINT64 pages = (size + 4095) / 4096;
            EFI_PHYSICAL_ADDRESS address = 0xFFFFFFFFULL;
            EFI_STATUS status = g_bs->AllocatePages(
                EFI_ALLOCATE_MAX_ADDRESS, EFI_LOADER_DATA, pages, &address);
            if (EFI_ERROR(status) || !address)
                status = g_bs->AllocatePages(EFI_ALLOCATE_ANY_PAGES,
                                             EFI_LOADER_DATA, pages,
                                             &address);
            if (!EFI_ERROR(status) && address &&
                iso_read_file(bio, extent, size, (void *)(UINTN)address)) {
                *physical = (UINT64)address;
                *bytes = size;
                boot_print("Font: /install/WQY.TTC from the ISO.\r\n");
                return;
            }
        }
    }

    UINT64 address = 0;
    UINT64 size = load_font_from_ext4(image_handle, &address);
    if (size && address) {
        *physical = address;
        *bytes = size;
        boot_print("Font: /WQY.TTC from ext4.\r\n");
    }
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
