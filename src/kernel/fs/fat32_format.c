/*
 * Zhaoxu OS - FAT32 formatting for the installer.
 *
 * Builds a 32-bit FAT volume from scratch (BPB, FSInfo, two FATs, root
 * cluster) and populates EFI/BOOT with the UEFI loader and the running
 * kernel.  The mounted /mnt volume is saved and restored around the work.
 */
#include "fat32_internal.h"

#include <block.h>
#include <mem.h>
#include <string.h>
#include <types.h>

static int write_sector(int disk, u64 lba, const u8 *buffer)
{
    return block_disk_write(disk, lba, 1, buffer);
}

static int zero_sectors(int disk, u64 lba, u32 sectors)
{
    u8 zero[512];
    memset(zero, 0, sizeof(zero));
    for (u32 i = 0; i < sectors; i++) {
        if (write_sector(disk, lba + i, zero) != 0)
            return -1;
    }
    return 0;
}

static void put16(u8 *p, u16 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}

static void put32(u8 *p, u32 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
    p[2] = (u8)(value >> 16);
    p[3] = (u8)(value >> 24);
}

static void put_label(u8 *destination, const char *label)
{
    int i = 0;
    while (i < 11 && label && label[i]) {
        destination[i] = (u8)label[i];
        i++;
    }
    while (i < 11)
        destination[i++] = ' ';
}

static void build_bpb(u8 *sector, u32 total, u32 fat_size, u32 hidden,
                      const char *label)
{
    memset(sector, 0, 512);
    sector[0] = 0xEB;
    sector[1] = 0x58;
    sector[2] = 0x90;
    memcpy(sector + 3, "ZHAOXU  ", 8);
    put16(sector + 11, 512);
    sector[13] = 1;
    put16(sector + 14, 32);
    sector[16] = 2;
    put16(sector + 17, 0);
    sector[21] = 0xF8;
    put16(sector + 22, 0);
    put16(sector + 24, 32);
    put16(sector + 26, 64);
    put32(sector + 28, hidden);
    put32(sector + 32, total);
    put32(sector + 36, fat_size);
    put16(sector + 40, 0);
    put16(sector + 42, 0);
    put32(sector + 44, 2);
    put16(sector + 48, 1);
    put16(sector + 50, 6);
    sector[64] = 0x80;
    sector[66] = 0x29;
    put32(sector + 67, 0x5A4F5355);
    put_label(sector + 71, label);
    memcpy(sector + 82, "FAT32   ", 8);
    sector[510] = 0x55;
    sector[511] = 0xAA;
}

static u32 compute_fat_size(u32 total)
{
    u32 reserved = 32;
    u32 fats = 2;
    u32 fat_size = 1;

    for (;;) {
        if (reserved + fats * fat_size >= total)
            return 0;
        u32 data = total - reserved - fats * fat_size;
        u32 clusters = data;
        u32 need = ((clusters + 2) * 4 + 511) / 512;
        if (need <= fat_size)
            return fat_size;
        fat_size = need;
    }
}

int fat32_format_volume(int disk, u64 volume_base, u32 sectors,
                        const char *label)
{
    if (sectors < 2048)
        return -1;

    u32 fat_size = compute_fat_size(sectors);
    if (!fat_size)
        return -1;
    u32 data_start = 32 + 2 * fat_size;
    u32 clusters = sectors - data_start - 1;   /* one cluster for root */

    u8 sector[512];
    build_bpb(sector, sectors, fat_size, (u32)volume_base, label);
    if (write_sector(disk, volume_base, sector) != 0)
        return -1;

    u8 fsinfo[512];
    memset(fsinfo, 0, sizeof(fsinfo));
    put32(fsinfo + 0, 0x41615252);
    put32(fsinfo + 484, 0x61417272);
    put32(fsinfo + 488, clusters);
    put32(fsinfo + 492, 3);
    put32(fsinfo + 508, 0xAA550000);
    if (write_sector(disk, volume_base + 1, fsinfo) != 0)
        return -1;

    if (zero_sectors(disk, volume_base + 2, 4) != 0)
        return -1;
    if (write_sector(disk, volume_base + 6, sector) != 0)
        return -1;
    if (write_sector(disk, volume_base + 7, fsinfo) != 0)
        return -1;

    for (u32 copy = 0; copy < 2; copy++) {
        u64 fat_lba = volume_base + 32 + (u64)copy * fat_size;
        if (zero_sectors(disk, fat_lba, fat_size) != 0)
            return -1;
        u8 entries[512];
        memset(entries, 0, sizeof(entries));
        put32(entries + 0, 0x0FFFFFF8);
        put32(entries + 4, 0x0FFFFFFF);
        put32(entries + 8, 0x0FFFFFFF);
        if (write_sector(disk, fat_lba, entries) != 0)
            return -1;
    }

    u64 root_lba = volume_base + data_start;
    if (zero_sectors(disk, root_lba, 1) != 0)
        return -1;
    memset(sector, 0, sizeof(sector));
    put_label(sector, label);
    sector[11] = 0x08;
    if (write_sector(disk, root_lba, sector) != 0)
        return -1;

    return block_disk_flush(disk);
}

int fat32_make_esp(int disk, u64 volume_base, u32 sectors, const u8 *bootx64,
                   u64 bootx64_size, const u8 *kernel, u64 kernel_size)
{
    struct fat32_fs saved = *fat32_state();
    int result = -1;

    if (!bootx64 || !bootx64_size || !kernel || !kernel_size)
        return -1;
    if (fat32_format_volume(disk, volume_base, sectors, "ZHAOXUEFI") != 0)
        return -1;
    if (fat32_mount_at(disk, volume_base) != 0)
        goto restore;
    fat32_alloc_reset();

    if (fat32_mkdir("/EFI") != 0 || fat32_mkdir("/EFI/BOOT") != 0)
        goto restore;
    if (fat32_write_file("/EFI/BOOT/BOOTX64.EFI", bootx64, bootx64_size) != 0)
        goto restore;
    if (fat32_write_file("/EFI/BOOT/KERNEL.ELF", kernel, kernel_size) != 0)
        goto restore;

    struct fat_entry entry;
    if (fat32_find("/EFI/BOOT/BOOTX64.EFI", &entry) != 0 ||
        entry.size != (u32)bootx64_size)
        goto restore;
    if (fat32_find("/EFI/BOOT/KERNEL.ELF", &entry) != 0 ||
        entry.size != (u32)kernel_size)
        goto restore;

    result = 0;

restore:
    *fat32_state() = saved;
    return result;
}

/* The data partition keeps runtime assets (currently the CJK font) out of
 * the ESP, so an installed system still renders Chinese without any extra
 * disk (rule 27). */
int fat32_make_data(int disk, u64 volume_base, u32 sectors, const u8 *font,
                    u64 font_size)
{
    struct fat32_fs saved = *fat32_state();
    int result = -1;

    if (fat32_format_volume(disk, volume_base, sectors, "ZHAOXUDATA") != 0)
        return -1;
    if (fat32_mount_at(disk, volume_base) != 0)
        goto restore;
    fat32_alloc_reset();

    if (font && font_size > 12) {
        if (fat32_write_file("/WQY.TTC", font, font_size) != 0)
            goto restore;
        struct fat_entry entry;
        if (fat32_find("/WQY.TTC", &entry) != 0 ||
            entry.size != (u32)font_size)
            goto restore;
    }

    result = 0;

restore:
    *fat32_state() = saved;
    return result;
}
