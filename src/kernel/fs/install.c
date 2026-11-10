/*
 * Zhaoxu OS - self-contained disk installer.
 *
 * No extra installation media is required (rule 27): the UEFI loader comes
 * from the initramfs (/boot/BOOTX64.EFI) and the kernel is the running
 * image.  The installer builds the ESP itself (FAT32 format + EFI/BOOT
 * files) and writes either an MBR or a GPT layout (rule 15):
 *
 *   MBR: LBA0 stage1, LBA1 stage2, kernel, gap, ESP at LBA 2048.
 *   GPT: LBA0 stage1 + protective MBR, LBA1/2 header+entries, stage2 at
 *        LBA34, kernel, gap, ESP at LBA 2048, backup GPT at the end.
 */
#include <block.h>
#include <boot.h>
#include <fat32.h>
#include <install.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define SECTOR_SIZE 512
#define CHUNK_SECTORS 8
#define CHUNK_BYTES (CHUNK_SECTORS * SECTOR_SIZE)

#define ESP_MAX_SECTORS 131072u        /* 64 MiB */
#define ESP_MIN_SECTORS 70000u         /* FAT32 needs >= 65525 clusters */
#define GPT_HEADER_LBA 1u
#define GPT_ENTRIES_LBA 2u
#define GPT_ENTRY_COUNT 128u
#define GPT_ENTRY_SIZE 128u
#define GPT_ENTRY_SECTORS 32u
#define GPT_BIOS_START 34u
#define GPT_BACKUP_SECTORS 33u

#define KERNEL_VMA 0xffffffff80000000ULL
#define KERNEL_MAP_SIZE 0x01000000ULL
#define KERNEL_FILE_HEADER 0x1000u

static const u8 g_guid_esp[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};
static const u8 g_guid_bios[16] = {
    0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6E, 0x6F,
    0x74, 0x4E, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49
};
static const u8 g_guid_disk[16] = {
    0x5A, 0x4F, 0x53, 0x2D, 0x44, 0x49, 0x53, 0x4B,
    0x9A, 0x4F, 0x53, 0x31, 0x00, 0x00, 0x00, 0x01
};
static const u8 g_guid_part[16] = {
    0x5A, 0x4F, 0x53, 0x2D, 0x50, 0x41, 0x52, 0x54,
    0x9A, 0x4F, 0x53, 0x31, 0x00, 0x00, 0x00, 0x02
};

extern const u8 bios_stage1_start[];
extern const u8 bios_stage1_end[];
extern const u8 bios_stage2_start[];
extern const u8 bios_stage2_end[];

static u8 g_sector[SECTOR_SIZE];
static u8 g_stage1[512];
static u8 g_gpt_entries[GPT_ENTRY_COUNT * GPT_ENTRY_SIZE];
static u8 g_chunk[CHUNK_BYTES];
static u8 g_zero[CHUNK_BYTES];
static u8 g_stage2[0x8000];
static char g_error[80];

static void set_error(const char *text)
{
    usize i = 0;
    for (; text[i] && i + 1 < sizeof(g_error); i++)
        g_error[i] = text[i];
    g_error[i] = 0;
}

const char *install_last_error(void)
{
    return g_error;
}

static void label_from_sector(const u8 *sector, char label[12])
{
    label[0] = 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return;
    if (sector[0] != 0xEB && sector[0] != 0xE9)
        return;
    u32 bytes_per_sector = (u32)sector[11] | ((u32)sector[12] << 8);
    if (bytes_per_sector != SECTOR_SIZE)
        return;

    u32 offset = 0;
    if (sector[0x36] == 'F' && sector[0x37] == 'A' &&
        sector[0x38] == 'T' && sector[0x39] == '1')
        offset = 0x2B;
    else if (sector[0x52] == 'F' && sector[0x53] == 'A' &&
             sector[0x54] == 'T' && sector[0x55] == '3')
        offset = 0x47;
    else
        return;

    int length = 11;
    while (length > 0 && sector[offset + length - 1] == ' ')
        length--;
    for (int i = 0; i < length; i++)
        label[i] = (char)sector[offset + i];
    label[length] = 0;
}

static int sector_is_blank(const u8 *sector)
{
    for (int i = 0; i < SECTOR_SIZE; i++)
        if (sector[i])
            return 0;
    return 1;
}

static u32 get_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static void put_le16(u8 *p, u16 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}

static void put_le32(u8 *p, u32 value)
{
    p[0] = (u8)(value & 0xFF);
    p[1] = (u8)((value >> 8) & 0xFF);
    p[2] = (u8)((value >> 16) & 0xFF);
    p[3] = (u8)((value >> 24) & 0xFF);
}

static void put_le64(u8 *p, u64 value)
{
    put_le32(p, (u32)value);
    put_le32(p + 4, (u32)(value >> 32));
}

/* Wrap the in-memory kernel image in a one-segment ELF so the UEFI loader
 * can validate and relocate it.  The image itself starts at KERNEL_VMA. */
static u8 *build_kernel_elf(const u8 *image, u64 size, u64 *file_size)
{
    u64 total = KERNEL_FILE_HEADER + size;
    u8 *file = kmalloc(total);
    if (!file)
        return NULL;
    memset(file, 0, KERNEL_FILE_HEADER);

    file[0] = 0x7F;
    file[1] = 'E';
    file[2] = 'L';
    file[3] = 'F';
    file[4] = 2;
    file[5] = 1;
    file[6] = 1;
    put_le16(file + 16, 2);
    put_le16(file + 18, 62);
    put_le32(file + 20, 1);
    put_le64(file + 24, KERNEL_VMA);
    put_le64(file + 32, 64);
    put_le16(file + 52, 64);
    put_le16(file + 54, 56);
    put_le16(file + 56, 1);

    u8 *header = file + 64;
    put_le32(header + 0, 1);
    put_le32(header + 4, 7);
    put_le64(header + 8, KERNEL_FILE_HEADER);
    put_le64(header + 16, KERNEL_VMA);
    put_le64(header + 24, KERNEL_VMA);
    put_le64(header + 32, size);
    put_le64(header + 40, KERNEL_MAP_SIZE);
    put_le64(header + 48, 0x1000);

    memcpy(file + KERNEL_FILE_HEADER, image, (usize)size);
    *file_size = total;
    return file;
}

static u64 blob_sectors(const u8 *start, const u8 *end)
{
    return ((u64)(end - start) + SECTOR_SIZE - 1) / SECTOR_SIZE;
}

/* A previous Zhaoxu install: our MBR (0x0C/0x0E ESP) or our GPT hybrid. */
static int sector_is_our_mbr(const u8 *sector)
{
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return 0;
    u8 type = sector[446 + 4];
    if (type == 0x0C || type == 0x0E)
        return get_le32(sector + 446 + 8) == INSTALL_PARTITION_LBA;
    if (type == 0xEE) {
        usize size = (usize)(bios_stage1_end - bios_stage1_start);
        static const u8 marker[5] = {'Z', 'S', 'L', 'B', 'A'};
        for (usize i = 0; i + 7 <= 446 && i + 7 <= size; i++) {
            if (memcmp(bios_stage1_start + i, marker, 5) != 0)
                continue;
            if (memcmp(sector, bios_stage1_start, i) != 0)
                return 0;
            return memcmp(sector + i + 7, bios_stage1_start + i + 7,
                          446 - (i + 7)) == 0;
        }
        return 0;
    }
    return 0;
}

int install_scan(struct install_disk_info *out, int capacity)
{
    int count = 0;
    int disks = block_disk_count();
    for (int i = 0; i < disks && count < capacity; i++) {
        struct install_disk_info *info = &out[count++];
        memset(info, 0, sizeof(*info));
        info->disk = i;
        info->port = block_disk_port(i);
        info->sectors = block_disk_sectors(i);
        const char *model = block_disk_model(i);
        usize n = 0;
        while (model[n] && n + 1 < sizeof(info->model)) {
            info->model[n] = model[n];
            n++;
        }
        info->model[n] = 0;

        int blank = 0;
        int installed = 0;
        if (block_disk_read(i, 0, 1, g_sector) == 0) {
            blank = sector_is_blank(g_sector);
            installed = !blank && sector_is_our_mbr(g_sector);
            label_from_sector(g_sector, info->label);
        }
        info->is_esp = strcmp(info->label, "ZHAOXUEFI") == 0;
        info->is_data = strcmp(info->label, "ZHAOXUDATA") == 0;
        info->is_target = !info->is_esp && !info->is_data &&
                          (blank || installed) &&
                          info->sectors >=
                              INSTALL_PARTITION_LBA + ESP_MIN_SECTORS;
    }
    return count;
}

int install_pick_target(void)
{
    struct install_disk_info disks[8];
    int count = install_scan(disks, 8);
    for (int i = 0; i < count; i++) {
        if (disks[i].is_target)
            return disks[i].disk;
    }
    return -1;
}

static u32 crc32(const u8 *data, usize length)
{
    u32 crc = 0xFFFFFFFFu;
    for (usize i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (u32)(-(int)(crc & 1)));
    }
    return ~crc;
}

static int patch_stage1(u16 stage2_lba)
{
    usize size = (usize)(bios_stage1_end - bios_stage1_start);
    if (size < 512)
        return -1;
    memcpy(g_stage1, bios_stage1_start, 512);
    static const u8 marker[5] = {'Z', 'S', 'L', 'B', 'A'};
    for (usize i = 0; i + 5 + 2 <= 446; i++) {
        if (memcmp(g_stage1 + i, marker, 5) != 0)
            continue;
        g_stage1[i + 5] = (u8)stage2_lba;
        g_stage1[i + 6] = (u8)(stage2_lba >> 8);
        return 0;
    }
    return -1;
}

static int prepare_stage2(u64 kernel_lba, u64 kernel_bytes, u64 kernel_sectors)
{
    static const u8 magic[8] = {'Z', 'B', 'O', 'O', 'T', 'P', 'R', 'M'};
    usize size = (usize)(bios_stage2_end - bios_stage2_start);
    if (size == 0 || size > sizeof(g_stage2))
        return -1;
    memcpy(g_stage2, bios_stage2_start, size);
    for (usize i = 0; i + 8 + 24 <= size; i++) {
        if (memcmp(g_stage2 + i, magic, 8) != 0)
            continue;
        u8 *p = g_stage2 + i + 8;
        put_le64(p, kernel_lba);
        put_le64(p + 8, kernel_bytes);
        put_le64(p + 16, kernel_sectors);
        return 0;
    }
    return -1;
}

static int write_blob(int disk, u64 lba, const u8 *data, u64 bytes)
{
    u64 offset = 0;
    while (offset < bytes) {
        u64 left = bytes - offset;
        u32 chunk = (u32)((left + SECTOR_SIZE - 1) / SECTOR_SIZE);
        if (chunk > CHUNK_SECTORS)
            chunk = CHUNK_SECTORS;
        u32 padded = chunk * SECTOR_SIZE;
        if (left < padded) {
            memset(g_chunk, 0, padded);
            memcpy(g_chunk, data + offset, (usize)left);
            if (block_disk_write(disk, lba + offset / SECTOR_SIZE, chunk,
                                g_chunk) != 0)
                return -1;
        } else if (block_disk_write(disk, lba + offset / SECTOR_SIZE, chunk,
                                   data + offset) != 0) {
            return -1;
        }
        offset += padded;
    }
    return 0;
}

static int zero_gap(int disk, u64 from, u64 to)
{
    memset(g_zero, 0, sizeof(g_zero));
    for (u64 lba = from; lba < to; lba += CHUNK_SECTORS) {
        u32 chunk = CHUNK_SECTORS;
        if (to - lba < chunk)
            chunk = (u32)(to - lba);
        if (block_disk_write(disk, lba, chunk, g_zero) != 0)
            return -1;
    }
    return 0;
}

static int write_mbr(int target, u64 total, u32 esp_sectors, int gpt)
{
    memset(g_sector, 0, SECTOR_SIZE);
    memcpy(g_sector, g_stage1, 446);
    u8 *entry = g_sector + 446;
    if (gpt) {
        entry[0] = 0x00;
        entry[1] = 0xFF;
        entry[2] = 0xFF;
        entry[3] = 0xFF;
        entry[4] = 0xEE;
        entry[5] = 0xFF;
        entry[6] = 0xFF;
        entry[7] = 0xFF;
        put_le32(entry + 8, 1);
        u64 size = total > 1 ? total - 1 : 1;
        if (size > 0xFFFFFFFFu)
            size = 0xFFFFFFFFu;
        put_le32(entry + 12, (u32)size);
    } else {
        entry[0] = 0x80;
        entry[1] = 0xFE;
        entry[2] = 0xFF;
        entry[3] = 0xFF;
        entry[4] = 0x0C;
        entry[5] = 0xFE;
        entry[6] = 0xFF;
        entry[7] = 0xFF;
        put_le32(entry + 8, INSTALL_PARTITION_LBA);
        put_le32(entry + 12, esp_sectors);
    }
    g_sector[510] = 0x55;
    g_sector[511] = 0xAA;
    return block_disk_write(target, 0, 1, g_sector);
}

static void gpt_entry(u8 *entry, const u8 type[16], const u8 unique[16],
                      u64 first, u64 last)
{
    memset(entry, 0, GPT_ENTRY_SIZE);
    memcpy(entry, type, 16);
    memcpy(entry + 16, unique, 16);
    put_le64(entry + 32, first);
    put_le64(entry + 40, last);
}

static int write_gpt_header(int disk, u64 lba, u64 current, u64 backup,
                            u64 entries_lba, u64 first_usable,
                            u64 last_usable, u32 entries_crc)
{
    memset(g_sector, 0, SECTOR_SIZE);
    memcpy(g_sector, "EFI PART", 8);
    put_le32(g_sector + 8, 0x00010000);
    put_le32(g_sector + 12, 92);
    put_le64(g_sector + 24, current);
    put_le64(g_sector + 32, backup);
    put_le64(g_sector + 40, first_usable);
    put_le64(g_sector + 48, last_usable);
    memcpy(g_sector + 56, g_guid_disk, 16);
    put_le64(g_sector + 72, entries_lba);
    put_le32(g_sector + 80, GPT_ENTRY_COUNT);
    put_le32(g_sector + 84, GPT_ENTRY_SIZE);
    put_le32(g_sector + 88, entries_crc);
    put_le32(g_sector + 16, crc32(g_sector, 92));
    return block_disk_write(disk, lba, 1, g_sector);
}

static int write_gpt(int disk, u64 total, u32 esp_sectors)
{
    memset(g_gpt_entries, 0, sizeof(g_gpt_entries));
    gpt_entry(g_gpt_entries, g_guid_bios, g_guid_part, GPT_BIOS_START,
              INSTALL_PARTITION_LBA - 1);
    gpt_entry(g_gpt_entries + GPT_ENTRY_SIZE, g_guid_esp, g_guid_part,
              INSTALL_PARTITION_LBA,
              (u64)INSTALL_PARTITION_LBA + esp_sectors - 1);

    u32 entries_crc = crc32(g_gpt_entries, sizeof(g_gpt_entries));
    u64 backup_entries = total - GPT_BACKUP_SECTORS;
    u64 last_usable = backup_entries - 1;

    if (write_blob(disk, GPT_ENTRIES_LBA, g_gpt_entries,
                   sizeof(g_gpt_entries)) != 0)
        return -1;
    if (write_blob(disk, backup_entries, g_gpt_entries,
                   sizeof(g_gpt_entries)) != 0)
        return -1;
    if (write_gpt_header(disk, GPT_HEADER_LBA, GPT_HEADER_LBA, total - 1,
                         GPT_ENTRIES_LBA, GPT_BIOS_START,
                         last_usable - GPT_BIOS_START + 1, entries_crc) != 0)
        return -1;
    return write_gpt_header(disk, total - 1, total - 1, GPT_HEADER_LBA,
                            backup_entries, GPT_BIOS_START,
                            last_usable - GPT_BIOS_START + 1, entries_crc);
}

static int read_stage1_verify(int disk)
{
    if (block_disk_read(disk, 0, 1, g_sector) != 0)
        return -1;
    if (memcmp(g_sector, g_stage1, 446) != 0)
        return -1;
    return 0;
}

int install_run(int target_disk, int gpt, void (*progress)(u32 done, u32 total))
{
    if (target_disk < 0) {
        set_error("invalid target disk");
        return -1;
    }

    struct install_disk_info disks[8];
    int count = install_scan(disks, 8);
    int is_target = 0;
    for (int i = 0; i < count; i++) {
        if (disks[i].disk == target_disk && disks[i].is_target)
            is_target = 1;
    }
    if (!is_target) {
        set_error("target is not a blank Zhaoxu target disk");
        return -1;
    }

    u64 total = block_disk_sectors(target_disk);
    u64 usable = total - INSTALL_PARTITION_LBA -
                 (gpt ? GPT_BACKUP_SECTORS : 0);
    if (usable < ESP_MIN_SECTORS) {
        set_error("target disk is too small");
        return -1;
    }
    u32 esp_sectors = usable > ESP_MAX_SECTORS ? ESP_MAX_SECTORS
                                               : (u32)usable;

    const struct boot_info *bi = boot_get_info();
    const u8 *kernel = (const u8 *)(uintptr_t)bi->kernel_phys_base;
    u64 kernel_bytes = bi->kernel_size;
    if (!kernel || kernel_bytes == 0) {
        set_error("running kernel image is unavailable");
        return -1;
    }

    u8 *loader = NULL;
    u64 loader_bytes = 0;
    if (vfs_read_all("/boot/BOOTX64.EFI", &loader, &loader_bytes) != 0 ||
        !loader || !loader_bytes) {
        set_error("embedded BOOTX64.EFI is missing");
        return -1;
    }

    u64 stage1_bytes = (u64)(bios_stage1_end - bios_stage1_start);
    u64 stage2_bytes = (u64)(bios_stage2_end - bios_stage2_start);
    if (stage1_bytes != SECTOR_SIZE || stage2_bytes == 0 ||
        stage2_bytes > sizeof(g_stage2)) {
        set_error("ZhaoxuBootloader payload is not embedded");
        kfree(loader);
        return -1;
    }

    u64 stage2_sectors = blob_sectors(bios_stage2_start, bios_stage2_end);
    u64 stage2_lba = gpt ? GPT_BIOS_START : 1;
    u64 kernel_lba = stage2_lba + stage2_sectors;
    u64 kernel_sectors = blob_sectors(kernel, kernel + kernel_bytes);
    if (kernel_sectors > 0x380) {
        set_error("kernel too large for the legacy BIOS loader");
        kfree(loader);
        return -1;
    }
    if (kernel_lba + kernel_sectors > INSTALL_PARTITION_LBA) {
        set_error("kernel does not fit before the partition");
        kfree(loader);
        return -1;
    }
    if (patch_stage1((u16)stage2_lba) != 0 ||
        prepare_stage2(kernel_lba, kernel_bytes, kernel_sectors) != 0) {
        set_error("stage 1/2 parameter blocks are missing");
        kfree(loader);
        return -1;
    }

    u64 gap_start = kernel_lba + kernel_sectors;
    u32 steps = 8;
    u32 step = 0;

    if (write_mbr(target_disk, total, esp_sectors, gpt) != 0) {
        set_error("failed to write the MBR");
        kfree(loader);
        return -1;
    }
    step++;
    if (progress)
        progress(step, steps);

    if (gpt && write_gpt(target_disk, total, esp_sectors) != 0) {
        set_error("failed to write the GPT");
        kfree(loader);
        return -1;
    }

    if (write_blob(target_disk, stage2_lba, g_stage2, stage2_bytes) != 0) {
        set_error("failed to write bootloader stage 2");
        kfree(loader);
        return -1;
    }
    step++;
    if (progress)
        progress(step, steps);

    if (write_blob(target_disk, kernel_lba, kernel, kernel_bytes) != 0) {
        set_error("failed to write the kernel image");
        kfree(loader);
        return -1;
    }
    step++;
    if (progress)
        progress(step, steps);

    if (zero_gap(target_disk, gap_start, INSTALL_PARTITION_LBA) != 0) {
        set_error("failed to clear the partition gap");
        kfree(loader);
        return -1;
    }
    step++;
    if (progress)
        progress(step, steps);

    u64 kernel_file_size = 0;
    u8 *kernel_file = build_kernel_elf(kernel, kernel_bytes, &kernel_file_size);
    if (!kernel_file) {
        set_error("cannot build the kernel ELF image");
        kfree(loader);
        return -1;
    }
    const struct boot_info *info = boot_get_info();
    const u8 *font = (const u8 *)(uintptr_t)info->font_phys;
    u64 font_size = info->font_size;
    u8 *font_copy = NULL;
    if (!font || font_size < 13) {
        u64 read = 0;
        if (vfs_read_all("/mnt/WQY.TTC", &font_copy, &read) == 0) {
            font = font_copy;
            font_size = read;
        } else {
            font = NULL;
            font_size = 0;
        }
    }

    if (fat32_make_esp(target_disk, INSTALL_PARTITION_LBA, esp_sectors, loader,
                       loader_bytes, kernel_file, kernel_file_size, font,
                       font_size) != 0) {
        set_error("failed to build the FAT32 ESP");
        kfree(font_copy);
        kfree(kernel_file);
        kfree(loader);
        return -1;
    }
    kfree(font_copy);
    kfree(kernel_file);
    kfree(loader);
    step++;
    if (progress)
        progress(step, steps);

    if (read_stage1_verify(target_disk) != 0) {
        set_error("verification failed: bootloader MBR differs");
        return -1;
    }
    if (block_disk_read(target_disk, INSTALL_PARTITION_LBA, 1, g_sector) != 0 ||
        g_sector[510] != 0x55 || g_sector[511] != 0xAA ||
        memcmp(g_sector + 82, "FAT32   ", 8) != 0) {
        set_error("verification failed: ESP is not FAT32");
        return -1;
    }
    step++;
    if (progress)
        progress(step, steps);

    if (block_disk_flush(target_disk) != 0) {
        set_error("failed to flush the target disk");
        return -1;
    }
    step++;
    if (progress)
        progress(step, steps);

    g_error[0] = 0;
    return 0;
}
