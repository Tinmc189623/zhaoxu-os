/*
 * Zhaoxu OS - disk installer
 *
 * Produces a dual-boot disk: the MBR carries the legacy BIOS stage 1
 * (ZhaoxuBootloader), stage 2 and the raw running kernel follow in the
 * pre-partition gap, and the bootable FAT16 ESP image (a plain superfloppy
 * labelled ZHAOXUEFI, shipped as out/efi.img and attached as a read-only
 * disk) is copied verbatim into a partition starting at LBA 2048 for UEFI
 * firmware.  Nothing here parses FAT: every source and destination is
 * addressed as raw sectors through AHCI.
 */
#include <ahci.h>
#include <boot.h>
#include <install.h>
#include <kprintf.h>
#include <string.h>
#include <types.h>

#define SECTOR_SIZE 512
#define CHUNK_SECTORS 8
#define CHUNK_BYTES (CHUNK_SECTORS * SECTOR_SIZE)

/* ZhaoxuBootloader stage blobs, embedded by the linker (.biosblob). */
extern const u8 bios_stage1_start[];
extern const u8 bios_stage1_end[];
extern const u8 bios_stage2_start[];
extern const u8 bios_stage2_end[];

static u8 g_sector[SECTOR_SIZE];
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

/* A previous Zhaoxu install: our MBR with the single 0x0E partition. */
static int sector_is_our_mbr(const u8 *sector)
{
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return 0;
    if (sector[446 + 4] != 0x0E)
        return 0;
    return get_le32(sector + 446 + 8) == INSTALL_PARTITION_LBA;
}

int install_scan(struct install_disk_info *out, int capacity)
{
    int count = 0;
    int disks = ahci_disk_count();
    for (int i = 0; i < disks && count < capacity; i++) {
        struct install_disk_info *info = &out[count++];
        memset(info, 0, sizeof(*info));
        info->disk = i;
        info->port = ahci_disk_port(i);
        info->sectors = ahci_disk_sectors(i);
        const char *model = ahci_disk_model(i);
        usize n = 0;
        while (model[n] && n + 1 < sizeof(info->model)) {
            info->model[n] = model[n];
            n++;
        }
        info->model[n] = 0;

        int blank = 0;
        int installed = 0;
        if (ahci_disk_read(i, 0, 1, g_sector) == 0) {
            blank = sector_is_blank(g_sector);
            installed = !blank && sector_is_our_mbr(g_sector);
            label_from_sector(g_sector, info->label);
        }
        info->is_esp = strcmp(info->label, "ZHAOXUEFI") == 0;
        info->is_data = strcmp(info->label, "ZHAOXUDATA") == 0;
        info->is_target = !info->is_esp && !info->is_data &&
                          (blank || installed) &&
                          info->sectors >= INSTALL_PARTITION_LBA + (1 << 15);
    }
    return count;
}

static u64 source_total_sectors(int disk)
{
    if (ahci_disk_read(disk, 0, 1, g_sector) != 0)
        return 0;
    u32 total16 = (u32)g_sector[0x13] | ((u32)g_sector[0x14] << 8);
    u32 total32 = (u32)g_sector[0x20] | ((u32)g_sector[0x21] << 8) |
                  ((u32)g_sector[0x22] << 16) | ((u32)g_sector[0x23] << 24);
    u64 total = total16 ? total16 : total32;
    if (total > ahci_disk_sectors(disk))
        total = ahci_disk_sectors(disk);
    return total;
}

int install_source_disk(void)
{
    struct install_disk_info disks[8];
    int count = install_scan(disks, 8);
    for (int i = 0; i < count; i++)
        if (disks[i].is_esp)
            return disks[i].disk;
    return -1;
}

u64 install_source_sectors(void)
{
    int disk = install_source_disk();
    if (disk < 0)
        return 0;
    return source_total_sectors(disk);
}

int install_pick_target(int source_disk)
{
    struct install_disk_info disks[8];
    int count = install_scan(disks, 8);
    u64 needed = source_total_sectors(source_disk);
    if (needed == 0)
        return -1;
    needed += INSTALL_PARTITION_LBA;
    for (int i = 0; i < count; i++) {
        if (disks[i].disk == source_disk || !disks[i].is_target)
            continue;
        if (disks[i].sectors < needed)
            continue;
        return disks[i].disk;
    }
    return -1;
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

static u64 blob_sectors(const u8 *start, const u8 *end)
{
    return ((u64)(end - start) + SECTOR_SIZE - 1) / SECTOR_SIZE;
}

/* Patch the stage 2 parameter block so it knows where the kernel lives. */
static int prepare_stage2(u64 kernel_lba, u64 kernel_bytes, u64 kernel_sectors)
{
    static const u8 magic[8] = { 'Z', 'B', 'O', 'O', 'T', 'P', 'R', 'M' };
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
            if (ahci_disk_write(disk, lba + offset / SECTOR_SIZE, chunk,
                                g_chunk) != 0)
                return -1;
        } else if (ahci_disk_write(disk, lba + offset / SECTOR_SIZE, chunk,
                                   data + offset) != 0) {
            return -1;
        }
        offset += padded;
    }
    return 0;
}

static int write_mbr(int target, u64 total)
{
    u64 stage1_size = (u64)(bios_stage1_end - bios_stage1_start);
    if (stage1_size != SECTOR_SIZE)
        return -1;

    memset(g_sector, 0, SECTOR_SIZE);
    memcpy(g_sector, bios_stage1_start, 446);
    u8 *entry = g_sector + 446;
    entry[0] = 0x80;                /* bootable for legacy BIOS firmware */
    entry[1] = 0xFE;                /* CHS start (LBA is authoritative) */
    entry[2] = 0xFF;
    entry[3] = 0xFF;
    entry[4] = 0x0E;                /* FAT16 LBA */
    entry[5] = 0xFE;
    entry[6] = 0xFF;
    entry[7] = 0xFF;
    put_le32(entry + 8, INSTALL_PARTITION_LBA);
    put_le32(entry + 12, (u32)total);
    g_sector[510] = 0x55;
    g_sector[511] = 0xAA;
    return ahci_disk_write(target, 0, 1, g_sector);
}

int install_run(int source_disk, int target_disk,
                void (*progress)(u32 done, u32 total))
{
    if (source_disk < 0 || target_disk < 0 || source_disk == target_disk) {
        set_error("invalid source or target disk");
        return -1;
    }

    u64 total = source_total_sectors(source_disk);
    if (total < 16) {
        set_error("source ESP image is missing or corrupt");
        return -1;
    }
    if (ahci_disk_sectors(target_disk) < total + INSTALL_PARTITION_LBA) {
        set_error("target disk is too small");
        return -1;
    }
    if (ahci_disk_read(target_disk, 0, 1, g_sector) != 0 ||
        (!sector_is_blank(g_sector) && !sector_is_our_mbr(g_sector))) {
        set_error("target is not blank (refusing to overwrite)");
        return -1;
    }

    const struct boot_info *bi = boot_get_info();
    const u8 *kernel = (const u8 *)(uintptr_t)bi->kernel_phys_base;
    u64 kernel_bytes = bi->kernel_size;
    if (!kernel || kernel_bytes == 0) {
        set_error("running kernel image is unavailable");
        return -1;
    }
    u64 stage1_bytes = (u64)(bios_stage1_end - bios_stage1_start);
    u64 stage2_bytes = (u64)(bios_stage2_end - bios_stage2_start);
    if (stage1_bytes != SECTOR_SIZE || stage2_bytes == 0 ||
        stage2_bytes > sizeof(g_stage2)) {
        set_error("ZhaoxuBootloader payload is not embedded");
        return -1;
    }

    u64 stage2_sectors = blob_sectors(bios_stage2_start, bios_stage2_end);
    u64 kernel_lba = 1 + stage2_sectors;
    u64 kernel_sectors = blob_sectors(kernel, kernel + kernel_bytes);
    if (kernel_sectors > 0x380) {       /* stage 2 loads the kernel at 0x10000 */
        set_error("kernel too large for the legacy BIOS loader");
        return -1;
    }
    if (kernel_lba + kernel_sectors > INSTALL_PARTITION_LBA) {
        set_error("kernel does not fit before the partition");
        return -1;
    }
    if (prepare_stage2(kernel_lba, kernel_bytes, kernel_sectors) != 0) {
        set_error("stage 2 parameter block is missing");
        return -1;
    }

    u32 s2_chunks = (u32)((stage2_sectors + CHUNK_SECTORS - 1) /
                          CHUNK_SECTORS);
    u32 kernel_chunks = (u32)((kernel_sectors + CHUNK_SECTORS - 1) /
                              CHUNK_SECTORS);
    u64 gap_start = kernel_lba + kernel_sectors;
    u64 gap_sectors = INSTALL_PARTITION_LBA - gap_start;
    u32 gap_chunks = (u32)((gap_sectors + CHUNK_SECTORS - 1) / CHUNK_SECTORS);
    u32 copy_chunks = (u32)((total + CHUNK_SECTORS - 1) / CHUNK_SECTORS);
    u32 steps = 1 + s2_chunks + kernel_chunks + gap_chunks + copy_chunks + 2;
    u32 step = 0;

    if (write_mbr(target_disk, total) != 0) {
        set_error("failed to write the MBR and partition table");
        return -1;
    }
    step++;
    if (progress)
        progress(step, steps);

    if (write_blob(target_disk, 1, g_stage2,
                   (u64)(bios_stage2_end - bios_stage2_start)) != 0) {
        set_error("failed to write bootloader stage 2");
        return -1;
    }
    step += s2_chunks;
    if (progress)
        progress(step, steps);

    if (write_blob(target_disk, kernel_lba, kernel, kernel_bytes) != 0) {
        set_error("failed to write the kernel image");
        return -1;
    }
    step += kernel_chunks;
    if (progress)
        progress(step, steps);

    memset(g_zero, 0, CHUNK_BYTES);
    for (u64 i = 0; i < gap_sectors; i += CHUNK_SECTORS) {
        u32 chunk = CHUNK_SECTORS;
        if (gap_sectors - i < chunk)
            chunk = (u32)(gap_sectors - i);
        if (ahci_disk_write(target_disk, gap_start + i, chunk, g_zero) != 0) {
            set_error("failed to clear the partition gap");
            return -1;
        }
        step++;
        if (progress && (i & 127) == 0)
            progress(step, steps);
    }

    for (u32 i = 0; i < copy_chunks; i++) {
        u32 chunk = CHUNK_SECTORS;
        u64 offset = (u64)i * CHUNK_SECTORS;
        if (offset + chunk > total)
            chunk = (u32)(total - offset);
        if (ahci_disk_read(source_disk, offset, chunk, g_chunk) != 0) {
            set_error("failed to read the source ESP image");
            return -1;
        }
        if (ahci_disk_write(target_disk, INSTALL_PARTITION_LBA + offset,
                            chunk, g_chunk) != 0) {
            set_error("failed to write the target disk");
            return -1;
        }
        step++;
        if (progress)
            progress(step, steps);
    }

    if (ahci_disk_read(target_disk, 0, 1, g_sector) != 0 ||
        memcmp(g_sector, bios_stage1_start, 446) != 0) {
        set_error("verification failed: bootloader MBR differs");
        return -1;
    }
    if (ahci_disk_read(source_disk, 0, 1, g_chunk) != 0 ||
        ahci_disk_read(target_disk, INSTALL_PARTITION_LBA, 1, g_sector) != 0 ||
        memcmp(g_chunk, g_sector, SECTOR_SIZE) != 0) {
        set_error("verification failed: partition start differs");
        return -1;
    }
    step++;
    if (progress)
        progress(step, steps);

    if (ahci_disk_flush(target_disk) != 0) {
        set_error("failed to flush the target disk");
        return -1;
    }
    step++;
    if (progress)
        progress(step, steps);

    g_error[0] = 0;
    return 0;
}
