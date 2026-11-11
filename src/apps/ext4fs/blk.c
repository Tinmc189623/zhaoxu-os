/*
 * Zhaoxu OS - ext4fs service block I/O.
 *
 * Maps the ext4 core's struct ext4_env onto the trusted block syscalls
 * (38..42) and supplies the Ring3 console sink that kernel/lib/printf.c
 * expects from kprintf.
 */
#include "ext4_env.h"

#include <ext4fs.h>
#include <string.h>
#include <types.h>
#include <userlib.h>

#define BLK_SECTOR 512u
#define BLK_MAX_SECTORS 8u

/* kernel/lib/printf.c calls these from kprintf; in Ring3 the log is fd 1. */
void console_write(const char *text)
{
    if (!text)
        return;
    sys_write(1, text, u_strlen(text));
}

void console_write_n(const char *text, usize length)
{
    if (!text || !length)
        return;
    sys_write(1, text, (u64)length);
}

static int blk_env_read(int disk, u64 base, u64 offset, void *buffer,
                        u32 length)
{
    u8 *out = (u8 *)buffer;
    u64 lba = base + offset / BLK_SECTOR;
    u32 head = (u32)(offset % BLK_SECTOR);
    u32 remaining = length;
    u8 bounce[BLK_SECTOR];

    while (remaining) {
        if (head || remaining < BLK_SECTOR) {
            u32 take = BLK_SECTOR - head;
            if (take > remaining)
                take = remaining;
            if (sys_blk_read(disk, lba, 1, bounce) != 1)
                return -1;
            memcpy(out, bounce + head, take);
            out += take;
            remaining -= take;
            lba += 1;
            head = 0;
            continue;
        }

        u32 sectors = remaining / BLK_SECTOR;
        if (sectors > BLK_MAX_SECTORS)
            sectors = BLK_MAX_SECTORS;
        if (sys_blk_read(disk, lba, sectors, out) != (long)sectors)
            return -1;
        out += (u64)sectors * BLK_SECTOR;
        lba += sectors;
        remaining -= sectors * BLK_SECTOR;
    }
    return 0;
}

static int blk_env_write(int disk, u64 base, u64 offset, const void *buffer,
                         u32 length)
{
    const u8 *in = (const u8 *)buffer;
    u64 lba = base + offset / BLK_SECTOR;
    u32 head = (u32)(offset % BLK_SECTOR);
    u32 remaining = length;
    u8 bounce[BLK_SECTOR];

    while (remaining) {
        if (head || remaining < BLK_SECTOR) {
            u32 take = BLK_SECTOR - head;
            if (take > remaining)
                take = remaining;
            /* Partial sector: read-modify-write so the neighbours survive. */
            if (sys_blk_read(disk, lba, 1, bounce) != 1)
                return -1;
            memcpy(bounce + head, in, take);
            if (sys_blk_write(disk, lba, 1, bounce) != 1)
                return -1;
            in += take;
            remaining -= take;
            lba += 1;
            head = 0;
            continue;
        }

        u32 sectors = remaining / BLK_SECTOR;
        if (sectors > BLK_MAX_SECTORS)
            sectors = BLK_MAX_SECTORS;
        if (sys_blk_write(disk, lba, sectors, in) != (long)sectors)
            return -1;
        in += (u64)sectors * BLK_SECTOR;
        lba += sectors;
        remaining -= sectors * BLK_SECTOR;
    }
    return 0;
}

static int blk_env_flush(int disk)
{
    return sys_blk_flush(disk) == 0 ? 0 : -1;
}

static void blk_env_log(const char *text)
{
    console_write(text);
}

static int blk_env_disk_count(void)
{
    long count = sys_blk_count();
    return count > 0 ? (int)count : 0;
}

static u64 blk_env_disk_sectors(int disk)
{
    u64 sectors = 0;
    u64 port = 0;
    if (sys_blk_info(disk, &sectors, &port) != 0)
        return 0;
    return sectors;
}

void ext4fs_block_env_install(void)
{
    static const struct ext4_env env = {
        .read = blk_env_read,
        .write = blk_env_write,
        .flush = blk_env_flush,
        .alloc = arena_alloc,
        .free = arena_free,
        .log = blk_env_log,
        .disk_count = blk_env_disk_count,
        .disk_sectors = blk_env_disk_sectors,
    };
    ext4_env_set(&env);
}
