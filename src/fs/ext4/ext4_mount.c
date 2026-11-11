/*
 * ext4 core - mount, superblock probing and block access.
 *
 * Everything here is OS independent: block I/O, memory and logging all go
 * through the struct ext4_env vtable, so the same code runs inside the
 * kernel, inside the Ring3 ext4fs service and in the host test harness.
 */
#include "ext4_core.h"

#include <string.h>
#include <types.h>

struct ext4_fs g_ext4;

static int raw_read(int disk, u64 base, u64 offset, void *buffer, u32 length)
{
    return ext4_env()->read(disk, base, offset, buffer, length);
}

int ext4_read_block(u64 block, void *buffer)
{
    const struct ext4_env *env = ext4_env();
    if (!g_ext4.mounted || !env->read)
        return -1;
    return env->read(g_ext4.disk, g_ext4.volume_base,
                     block * g_ext4.block_size, buffer,
                     g_ext4.block_size);
}

int ext4_write_block(u64 block, const void *buffer)
{
    const struct ext4_env *env = ext4_env();
    if (!g_ext4.mounted || !g_ext4.writable || !env->write)
        return -1;
    return env->write(g_ext4.disk, g_ext4.volume_base,
                      block * g_ext4.block_size, buffer,
                      g_ext4.block_size);
}

int ext4_read_group_desc(u32 group, u8 *desc)
{
    if (group >= g_ext4.groups || !desc)
        return -1;
    u64 offset = (u64)g_ext4.gdt_block * g_ext4.block_size +
                 (u64)group * g_ext4.desc_size;
    u64 block = offset / g_ext4.block_size;
    u32 within = (u32)(offset % g_ext4.block_size);
    u8 *buffer = ext4_alloc(g_ext4.block_size);
    if (!buffer)
        return -1;
    int result = ext4_read_block(block, buffer);
    if (result == 0)
        memcpy(desc, buffer + within, g_ext4.desc_size);
    ext4_free(buffer);
    return result;
}

int ext4_read_inode_raw(u32 ino, u8 *inode)
{
    if (!g_ext4.mounted || ino == 0 || ino > g_ext4.inodes_count)
        return -1;

    u32 group = (ino - 1) / g_ext4.inodes_per_group;
    u32 index = (ino - 1) % g_ext4.inodes_per_group;
    if (group >= g_ext4.groups)
        return -1;

    u8 desc[EXT4_GD_SIZE64];
    if (ext4_read_group_desc(group, desc) != 0)
        return -1;

    u64 table = ext4_gd_inode_table(desc);
    u64 offset = table * g_ext4.block_size +
                 (u64)index * g_ext4.inode_size;
    u64 block = offset / g_ext4.block_size;
    u32 within = (u32)(offset % g_ext4.block_size);
    u32 bytes = g_ext4.inode_size < 256 ? g_ext4.inode_size : 256;
    if (within + bytes > g_ext4.block_size) {
        u8 *buffer = ext4_alloc(g_ext4.block_size);
        if (!buffer)
            return -1;
        int result = ext4_read_block(block, buffer);
        if (result == 0)
            memcpy(inode, buffer + within, g_ext4.block_size - within);
        ext4_free(buffer);
        bytes -= g_ext4.block_size - within;
        if (result != 0)
            return -1;
        u8 *tail = ext4_alloc(g_ext4.block_size);
        if (!tail)
            return -1;
        result = ext4_read_block(block + 1, tail);
        if (result == 0)
            memcpy(inode + (g_ext4.block_size - within), tail, bytes);
        ext4_free(tail);
        return result;
    }

    u8 *buffer = ext4_alloc(g_ext4.block_size);
    if (!buffer)
        return -1;
    int result = ext4_read_block(block, buffer);
    if (result == 0)
        memcpy(inode, buffer + within, bytes);
    ext4_free(buffer);
    return result;
}

int ext4_mounted(void)
{
    return g_ext4.mounted;
}

const char *ext4_label(void)
{
    return g_ext4.mounted ? g_ext4.label : NULL;
}

static void ext4_clear_state(void)
{
    memset(&g_ext4, 0, sizeof(g_ext4));
}

static int ext4_read_super(int disk, u64 base, u8 *sb)
{
    if (raw_read(disk, base, EXT4_SB_OFFSET, sb, 1024) != 0)
        return -1;
    if (ext4_le16(sb + 0x38) != EXT4_MAGIC)
        return -1;
    u32 log = ext4_le32(sb + 0x18);
    if (log > 2)
        return -1;
    u32 block_size = 1024u << log;
    if (block_size != 1024 && block_size != 2048 && block_size != 4096)
        return -1;
    return 0;
}

static int ext4_fill_fs(int disk, u64 base, const u8 *sb)
{
    ext4_clear_state();
    g_ext4.disk = disk;
    g_ext4.volume_base = base;
    g_ext4.block_size = 1024u << ext4_le32(sb + 0x18);
    g_ext4.sectors_per_block = g_ext4.block_size / 512;
    g_ext4.inodes_count = ext4_le32(sb + 0x00);
    g_ext4.blocks_count = ext4_le32(sb + 0x04);
    g_ext4.free_blocks = ext4_le32(sb + 0x0C);
    g_ext4.free_inodes = ext4_le32(sb + 0x10);
    g_ext4.first_data_block = ext4_le32(sb + 0x14);
    g_ext4.blocks_per_group = ext4_le32(sb + 0x20);
    g_ext4.inodes_per_group = ext4_le32(sb + 0x28);
    g_ext4.feature_incompat = ext4_le32(sb + 0x60);
    g_ext4.feature_ro_compat = ext4_le32(sb + 0x64);

    if (g_ext4.feature_incompat & EXT4_INCOMPAT_64BIT) {
        u32 blocks_hi = ext4_le32(sb + 0x150);
        u32 free_hi = ext4_le32(sb + 0x158);
        if (blocks_hi || free_hi)
            return -1;
        g_ext4.desc_size = ext4_le16(sb + 0xFE);
        if (g_ext4.desc_size < EXT4_GD_SIZE64)
            g_ext4.desc_size = EXT4_GD_SIZE64;
    } else {
        g_ext4.desc_size = EXT4_GD_SIZE32;
    }

    g_ext4.inode_size = ext4_le32(sb + 0x4C) >= 1 ? ext4_le16(sb + 0x58) : 128;
    if (g_ext4.inode_size < 128)
        g_ext4.inode_size = 128;
    g_ext4.first_ino = ext4_le32(sb + 0x4C) >= 1 ? ext4_le32(sb + 0x54) : 11;
    if (!g_ext4.first_ino)
        g_ext4.first_ino = 11;

    if (!g_ext4.inodes_per_group || !g_ext4.blocks_per_group ||
        !g_ext4.inodes_count || !g_ext4.blocks_count)
        return -1;
    if ((g_ext4.feature_incompat & EXT4_INCOMPAT_EXTENTS) == 0)
        return -1;
    if (g_ext4.feature_incompat &
        (EXT4_INCOMPAT_META_BG | EXT4_INCOMPAT_INLINE_DATA |
         EXT4_INCOMPAT_ENCRYPT))
        return -1;

    u32 data_blocks = g_ext4.blocks_count > g_ext4.first_data_block
                          ? g_ext4.blocks_count - g_ext4.first_data_block
                          : 0;
    g_ext4.groups =
        (data_blocks + g_ext4.blocks_per_group - 1) / g_ext4.blocks_per_group;
    if (!g_ext4.groups)
        return -1;
    g_ext4.gdt_block = g_ext4.first_data_block + 1;

    memset(g_ext4.label, 0, sizeof(g_ext4.label));
    memcpy(g_ext4.label, sb + 0x78, 16);
    for (int i = 15; i >= 0 && (g_ext4.label[i] == 0 ||
                                g_ext4.label[i] == ' '); i--)
        g_ext4.label[i] = 0;

    g_ext4.writable = g_ext4.block_size == 4096 &&
                      g_ext4.desc_size == EXT4_GD_SIZE32 &&
                      !(g_ext4.feature_ro_compat & EXT4_RO_METADATA_CSUM);
    g_ext4.mounted = 1;
    g_ext4.block_hint = 0;
    g_ext4.inode_hint = EXT4_LOST_FOUND_INO + 1;
    return 0;
}

int ext4_mount_at(int disk, u64 volume_base)
{
    u8 sb[1024];
    if (ext4_read_super(disk, volume_base, sb) != 0)
        return -1;
    if (ext4_fill_fs(disk, volume_base, sb) != 0)
        return -1;
    return 0;
}

static int probe_volume(int disk, u64 base, int *score)
{
    u8 sb[1024];
    if (ext4_read_super(disk, base, sb) != 0)
        return -1;
    int value = 1;
    char label[17];
    memset(label, 0, sizeof(label));
    memcpy(label, sb + 0x78, 16);
    for (int i = 15; i >= 0 && (label[i] == 0 || label[i] == ' '); i--)
        label[i] = 0;
    if (strcmp(label, "ZHAOXUROOT") == 0)
        value = 2;
    *score = value;
    return 0;
}

static void consider(int disk, u64 base, int *best_disk, u64 *best_base,
                     int *best_score)
{
    int score = 0;
    if (probe_volume(disk, base, &score) != 0)
        return;
    if (score > *best_score) {
        *best_score = score;
        *best_disk = disk;
        *best_base = base;
    }
}

int ext4_mount(void)
{
    const struct ext4_env *env = ext4_env();
    if (!env->read || !env->disk_count || !env->disk_sectors)
        return -1;
    int disks = env->disk_count();
    int best_disk = -1;
    u64 best_base = 0;
    int best_score = 0;

    for (int disk = 0; disk < disks; disk++) {
        u64 sectors = env->disk_sectors(disk);
        if (sectors < 4)
            continue;

        consider(disk, 0, &best_disk, &best_base, &best_score);

        u8 sector[512];
        if (raw_read(disk, 0, 0, sector, sizeof(sector)) != 0 ||
            sector[510] != 0x55 || sector[511] != 0xAA)
            continue;

        if (sector[446 + 4] == 0xEE) {
            u8 header[512];
            if (raw_read(disk, 0, 512, header, sizeof(header)) != 0 ||
                memcmp(header, "EFI PART", 8) != 0)
                continue;
            u64 entries_lba = ext4_le64(header + 72);
            u32 entries = ext4_le32(header + 80);
            u32 entry_size = ext4_le32(header + 84);
            if (entry_size < 128 || entries > 128 || entries_lba >= sectors)
                continue;
            for (u32 i = 0; i < entries; i++) {
                u64 offset = (u64)i * entry_size;
                u32 within = (u32)(offset % 512);
                if (within + 128 > 512)
                    continue;
                u8 entry[512];
                if (raw_read(disk, 0, (entries_lba + offset / 512) * 512,
                             entry, sizeof(entry)) != 0)
                    continue;
                const u8 *item = entry + within;
                int used = 0;
                for (int k = 0; k < 16; k++)
                    used |= item[k];
                if (!used)
                    continue;
                u64 start = ext4_le64(item + 32);
                if (start == 0 || start >= sectors)
                    continue;
                consider(disk, start, &best_disk, &best_base, &best_score);
            }
        } else {
            for (int i = 0; i < 4; i++) {
                const u8 *entry = sector + 446 + i * 16;
                u8 type = entry[4];
                u64 start = ext4_le32(entry + 8);
                u32 count = ext4_le32(entry + 12);
                if (!type || !count || start == 0 || start >= sectors)
                    continue;
                consider(disk, start, &best_disk, &best_base, &best_score);
            }
        }
    }

    if (best_disk < 0)
        return -1;
    if (ext4_mount_at(best_disk, best_base) != 0)
        return -1;

    ext4_logf("[ext4] mounted \"%s\" on /mnt: disk %d, %u blocks, %u groups, "
              "%s\n",
              g_ext4.label, g_ext4.disk, g_ext4.blocks_count, g_ext4.groups,
              g_ext4.writable ? "rw" : "ro");
    return 0;
}

struct ext4_state ext4_state_save(void)
{
    struct ext4_state state;
    _Static_assert(sizeof(state.bytes) >= sizeof(g_ext4), "state too small");
    memcpy(state.bytes, &g_ext4, sizeof(g_ext4));
    return state;
}

void ext4_state_restore(struct ext4_state state)
{
    memcpy(&g_ext4, state.bytes, sizeof(g_ext4));
}
