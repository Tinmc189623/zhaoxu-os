/*
 * Zhaoxu OS - efivarfs.
 *
 * Reads UEFI variables through the RuntimeServices table captured by the
 * UEFI loader.  The kernel never calls SetVirtualAddressMap, so the firmware
 * code and data remain at their physical addresses and are reachable through
 * the low identity map.  On a legacy BIOS boot the table pointer is zero and
 * the mount is a legitimately empty directory.
 */
#include <boot.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define EFIVAR_MAX 64
#define EFIVAR_NAME_MAX 128
#define EFIVAR_FILE_MAX 192
#define EFIVAR_QUERY_MAX 256
#define EFIVAR_DATA_MAX (64u * 1024u)
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL

typedef u64 efi_status;
typedef u16 efi_char16;

typedef struct {
    u32 data1;
    u16 data2;
    u16 data3;
    u8 data4[8];
} efi_guid;

typedef efi_status (__attribute__((ms_abi)) *efi_get_variable_fn)(
    efi_char16 *name, efi_guid *guid, u32 *attributes, u64 *size,
    void *data);
typedef efi_status (__attribute__((ms_abi)) *efi_get_next_fn)(
    u64 *name_size, efi_char16 *name, efi_guid *guid);

struct efivar {
    int used;
    char file[EFIVAR_FILE_MAX];
    efi_char16 name[EFIVAR_NAME_MAX];
    efi_guid guid;
    u32 attributes;
    u64 size;
};

struct efivarfs {
    struct efivar vars[EFIVAR_MAX];
    int count;
};

static void guid_text(const efi_guid *guid, char *out, usize capacity)
{
    ksnprintf(out, capacity,
              "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
              guid->data1, guid->data2, guid->data3,
              guid->data4[0], guid->data4[1], guid->data4[2],
              guid->data4[3], guid->data4[4], guid->data4[5],
              guid->data4[6], guid->data4[7]);
}

static void name_text(const efi_char16 *name, char *out, usize capacity)
{
    usize used = 0;
    while (name[used] && used + 1 < capacity) {
        u16 c = name[used];
        out[used] = (c >= 0x20 && c <= 0x7E) ? (char)c : '?';
        used++;
    }
    out[used] = 0;
}

/* UEFI runtime services still use physical addresses because the kernel
 * never calls SetVirtualAddressMap.  A contiguous run of PMM pages is
 * identity mapped in the low window, so it can be passed directly. */
static u64 alloc_contiguous(u32 count)
{
    if (!count)
        return 0;
    u64 first = pmm_alloc_page();
    if (!first)
        return 0;
    for (u32 i = 1; i < count; i++) {
        u64 page = pmm_alloc_page();
        if (!page || page != first + (u64)i * PAGE_SIZE) {
            if (page)
                pmm_free_page(page);
            for (u32 j = 0; j < i; j++)
                pmm_free_page(first + (u64)j * PAGE_SIZE);
            return 0;
        }
    }
    return first;
}

static void free_contiguous(u64 base, u32 count)
{
    for (u32 i = 0; i < count; i++)
        pmm_free_page(base + (u64)i * PAGE_SIZE);
}

static void efivarfs_probe(struct efivarfs *fs)
{
    const struct boot_info *bi = boot_get_info();
    if (!bi || !bi->efi_runtime_services)
        return;

    efi_get_variable_fn get_variable =
        *(efi_get_variable_fn *)(uintptr_t)
            (bi->efi_runtime_services + 0x48);
    efi_get_next_fn get_next =
        *(efi_get_next_fn *)(uintptr_t)
            (bi->efi_runtime_services + 0x50);
    if (!get_variable || !get_next)
        return;

    u64 page = pmm_alloc_page();
    if (!page)
        return;
    efi_char16 *name = (efi_char16 *)(uintptr_t)page;
    efi_guid *guid = (efi_guid *)(uintptr_t)(page + 512);
    u32 *attributes = (u32 *)(uintptr_t)(page + 544);
    u64 *size = (u64 *)(uintptr_t)(page + 552);
    u64 *name_size = (u64 *)(uintptr_t)(page + 560);
    memset(name, 0, 512);
    memset(guid, 0, sizeof(*guid));

    for (int i = 0; i < EFIVAR_MAX; i++) {
        *name_size = 512;
        efi_status status = get_next(name_size, name, guid);
        if (status != 0)
            break;
        if (*name_size > 512)
            break;

        *size = 0;
        *attributes = 0;
        status = get_variable(name, guid, attributes, size, NULL);
        if (status != 0 && status != EFI_BUFFER_TOO_SMALL)
            continue;
        if (*size > EFIVAR_DATA_MAX)
            continue;

        struct efivar *var = &fs->vars[fs->count];
        var->used = 1;
        usize copy = 0;
        while (name[copy] && copy + 1 < EFIVAR_NAME_MAX) {
            var->name[copy] = name[copy];
            copy++;
        }
        var->name[copy] = 0;
        var->guid = *guid;
        var->attributes = *attributes;
        var->size = *size;

        char name_ascii[EFIVAR_NAME_MAX];
        char guid_ascii[40];
        name_text(var->name, name_ascii, sizeof(name_ascii));
        guid_text(&var->guid, guid_ascii, sizeof(guid_ascii));
        ksnprintf(var->file, sizeof(var->file), "%s-%s",
                  name_ascii, guid_ascii);
        fs->count++;
    }
    pmm_free_page(page);
}

static struct efivar *efivarfs_find(struct efivarfs *fs, const char *rel)
{
    if (!rel || !rel[0])
        return NULL;
    for (int i = 0; i < fs->count; i++) {
        if (fs->vars[i].used && strcmp(fs->vars[i].file, rel) == 0)
            return &fs->vars[i];
    }
    return NULL;
}

static int efivarfs_list(void *private, const char *rel,
                         vfs_list_cb callback, void *context)
{
    struct efivarfs *fs = private;
    if (!fs || (rel && rel[0]))
        return -1;
    for (int i = 0; i < fs->count; i++) {
        if (fs->vars[i].used)
            callback(fs->vars[i].file, fs->vars[i].size, 0, context);
    }
    return 0;
}

static int efivarfs_stat(void *private, const char *rel, int *is_dir,
                         u64 *size)
{
    struct efivarfs *fs = private;
    if (!fs)
        return -1;
    if (!rel || !rel[0]) {
        if (is_dir)
            *is_dir = 1;
        if (size)
            *size = 0;
        return 0;
    }
    struct efivar *var = efivarfs_find(fs, rel);
    if (!var)
        return -1;
    if (is_dir)
        *is_dir = 0;
    if (size)
        *size = var->size;
    return 0;
}

static int efivarfs_read(void *private, const char *rel, u64 offset,
                         void *buffer, u64 capacity, u64 *read_out)
{
    struct efivarfs *fs = private;
    const struct boot_info *bi = boot_get_info();
    struct efivar *var = efivarfs_find(fs, rel);
    if (!var || !bi || !bi->efi_runtime_services || (!buffer && capacity))
        return -1;
    if (read_out)
        *read_out = 0;
    if (offset >= var->size)
        return 0;

    efi_get_variable_fn get_variable =
        *(efi_get_variable_fn *)(uintptr_t)
            (bi->efi_runtime_services + 0x48);
    if (!get_variable)
        return -1;

    u32 pages = (u32)((var->size + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!pages)
        return 0;
    u64 control = pmm_alloc_page();
    u64 data_phys = alloc_contiguous(pages);
    if (!control || !data_phys) {
        if (control)
            pmm_free_page(control);
        if (data_phys)
            free_contiguous(data_phys, pages);
        return -1;
    }

    efi_char16 *name = (efi_char16 *)(uintptr_t)control;
    efi_guid *guid = (efi_guid *)(uintptr_t)(control + 512);
    u32 *attributes = (u32 *)(uintptr_t)(control + 544);
    u64 *size = (u64 *)(uintptr_t)(control + 552);
    memcpy(name, var->name, sizeof(var->name));
    *guid = var->guid;
    *attributes = 0;
    *size = var->size;

    efi_status status = get_variable(name, guid, attributes, size,
                                     (void *)(uintptr_t)data_phys);
    if (status != 0 || *size > var->size) {
        pmm_free_page(control);
        free_contiguous(data_phys, pages);
        return -1;
    }

    u64 available = *size - offset;
    u64 count = capacity < available ? capacity : available;
    if (count)
        memcpy(buffer, (const void *)(uintptr_t)(data_phys + offset),
               (usize)count);
    pmm_free_page(control);
    free_contiguous(data_phys, pages);
    if (read_out)
        *read_out = count;
    return 0;
}

static int efivarfs_write(void *private, const char *rel, const u8 *data,
                          u64 size)
{
    (void)private;
    (void)rel;
    (void)data;
    (void)size;
    return -1;
}

static int efivarfs_unlink(void *private, const char *rel)
{
    (void)private;
    (void)rel;
    return -1;
}

static const struct vfs_fs_ops g_efivarfs_ops = {
    .name = "efivarfs",
    .list = efivarfs_list,
    .stat = efivarfs_stat,
    .read = efivarfs_read,
    .write = efivarfs_write,
    .unlink = efivarfs_unlink,
};

int efivarfs_mount(void)
{
    struct efivarfs *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return -1;
    efivarfs_probe(fs);
    if (vfs_mount("/sys/firmware/efi/efivars", "efivarfs",
                  &g_efivarfs_ops, fs) != 0) {
        kfree(fs);
        return -1;
    }
    return 0;
}
