/*
 * ext4 core - block and inode allocation, inode helpers and superblock
 * bookkeeping.
 */
#include "ext4_core.h"

#include <string.h>
#include <types.h>

static u8 *g_bitmap;
static u32 g_bitmap_block;
static int g_bitmap_valid;
static int g_bitmap_dirty;

static void flush_bitmap(void)
{
    if (g_bitmap_valid && g_bitmap_dirty && g_ext4.writable)
        ext4_write_block(g_bitmap_block, g_bitmap);
    g_bitmap_dirty = 0;
}

void ext4_bitmap_invalidate(void)
{
    flush_bitmap();
    g_bitmap_valid = 0;
    g_bitmap_block = 0;
}

static int bitmap_load(u32 block)
{
    if (g_bitmap_valid && g_bitmap_block == block)
        return 0;
    flush_bitmap();
    if (!g_bitmap) {
        g_bitmap = ext4_alloc(g_ext4.block_size);
        if (!g_bitmap)
            return -1;
    }
    if (ext4_read_block(block, g_bitmap) != 0) {
        g_bitmap_valid = 0;
        return -1;
    }
    g_bitmap_block = block;
    g_bitmap_valid = 1;
    g_bitmap_dirty = 0;
    return 0;
}

static int bitmap_test(u32 bit)
{
    return (g_bitmap[bit / 8] >> (bit % 8)) & 1;
}

static void bitmap_set(u32 bit)
{
    g_bitmap[bit / 8] |= (u8)(1u << (bit % 8));
    g_bitmap_dirty = 1;
}

static void bitmap_clear(u32 bit)
{
    g_bitmap[bit / 8] &= (u8)~(1u << (bit % 8));
    g_bitmap_dirty = 1;
}

static u32 gd_block_bitmap(const u8 *desc)
{
    u64 value = ext4_le32(desc);
    if (g_ext4.desc_size >= EXT4_GD_SIZE64)
        value |= (u64)ext4_le32(desc + 0x20) << 32;
    return (u32)value;
}

static u32 gd_inode_bitmap(const u8 *desc)
{
    u64 value = ext4_le32(desc + 4);
    if (g_ext4.desc_size >= EXT4_GD_SIZE64)
        value |= (u64)ext4_le32(desc + 0x24) << 32;
    return (u32)value;
}

u32 ext4_gd_inode_table(const u8 *desc)
{
    u64 value = ext4_le32(desc + 8);
    if (g_ext4.desc_size >= EXT4_GD_SIZE64)
        value |= (u64)ext4_le32(desc + 0x28) << 32;
    return (u32)value;
}

static u16 gd_free_blocks(const u8 *desc)
{
    return ext4_le16(desc + 0x0C);
}

static void gd_set_free_blocks(u8 *desc, u16 value)
{
    ext4_put16(desc + 0x0C, value);
}

static u16 gd_free_inodes(const u8 *desc)
{
    return ext4_le16(desc + 0x0E);
}

static void gd_set_free_inodes(u8 *desc, u16 value)
{
    ext4_put16(desc + 0x0E, value);
}

u16 ext4_gd_used_dirs(const u8 *desc)
{
    return ext4_le16(desc + 0x10);
}

void ext4_gd_set_used_dirs(u8 *desc, u16 value)
{
    ext4_put16(desc + 0x10, value);
}

int ext4_write_group_desc(u32 group, const u8 *desc)
{
    if (!g_ext4.writable)
        return -1;
    u64 offset = (u64)g_ext4.gdt_block * g_ext4.block_size +
                 (u64)group * g_ext4.desc_size;
    u64 block = offset / g_ext4.block_size;
    u32 within = (u32)(offset % g_ext4.block_size);
    u8 *buffer = ext4_alloc(g_ext4.block_size);
    if (!buffer)
        return -1;
    int result = ext4_read_block(block, buffer);
    if (result == 0) {
        memcpy(buffer + within, desc, g_ext4.desc_size);
        result = ext4_write_block(block, buffer);
    }
    ext4_free(buffer);
    return result;
}

static u32 group_blocks_in_fs(u32 group)
{
    u64 start = (u64)group * g_ext4.blocks_per_group;
    if (start >= g_ext4.blocks_count)
        return 0;
    u64 left = g_ext4.blocks_count - start;
    return left > g_ext4.blocks_per_group ? g_ext4.blocks_per_group
                                          : (u32)left;
}

int ext4_alloc_run(u32 count, u32 *first)
{
    if (!g_ext4.writable || count == 0)
        return -1;

    u32 start = g_ext4.block_hint;
    u32 total_blocks = g_ext4.blocks_count;

    for (u32 pass = 0; pass < 2; pass++) {
        u32 group = pass == 0 ? start / g_ext4.blocks_per_group : 0;
        u32 from = pass == 0 ? start % g_ext4.blocks_per_group : 0;
        u32 limit = pass == 0 ? g_ext4.groups : start / g_ext4.blocks_per_group;
        for (u32 step = 0; step < limit; step++, group = (group + 1) % g_ext4.groups) {
            u32 in_group = group_blocks_in_fs(group);
            if (!in_group)
                continue;
            u8 desc[64];
            if (ext4_read_group_desc(group, desc) != 0)
                return -1;
            u32 bitmap_block = gd_block_bitmap(desc);
            if (bitmap_load(bitmap_block) != 0)
                return -1;

            u32 scan_start = (pass == 0 && step == 0) ? from : 0;
            if (scan_start >= in_group)
                scan_start = 0;

            for (u32 i = 0; i < in_group; i++) {
                u32 bit = (scan_start + i) % in_group;
                if (bit + count > in_group)
                    continue;
                int free = 1;
                for (u32 k = 0; k < count; k++) {
                    if (bitmap_test(bit + k)) {
                        free = 0;
                        i += k;
                        break;
                    }
                }
                if (!free)
                    continue;

                for (u32 k = 0; k < count; k++)
                    bitmap_set(bit + k);
                u16 group_free = gd_free_blocks(desc);
                if (group_free < count)
                    return -1;
                gd_set_free_blocks(desc, (u16)(group_free - count));
                if (ext4_write_group_desc(group, desc) != 0)
                    return -1;
                g_ext4.free_blocks -= count;
                *first = group * g_ext4.blocks_per_group + bit;
                g_ext4.block_hint = *first + count;
                if (g_ext4.block_hint >= total_blocks)
                    g_ext4.block_hint = 0;
                flush_bitmap();
                return 0;
            }
        }
        start = 0;
    }
    return -1;
}

void ext4_free_run(u32 first, u32 count)
{
    while (count) {
        u32 group = first / g_ext4.blocks_per_group;
        u32 bit = first % g_ext4.blocks_per_group;
        u32 in_group = group_blocks_in_fs(group);
        if (bit >= in_group)
            break;
        u32 chunk = count;
        if (bit + chunk > in_group)
            chunk = in_group - bit;
        if (chunk == 0)
            break;

        u8 desc[64];
        if (ext4_read_group_desc(group, desc) != 0)
            return;
        u32 bitmap_block = gd_block_bitmap(desc);
        if (bitmap_load(bitmap_block) != 0)
            return;
        for (u32 k = 0; k < chunk; k++)
            bitmap_clear(bit + k);
        u32 group_free = gd_free_blocks(desc);
        gd_set_free_blocks(desc, (u16)(group_free + chunk));
        if (ext4_write_group_desc(group, desc) != 0)
            return;
        if (g_ext4.free_blocks + chunk > g_ext4.blocks_count)
            g_ext4.free_blocks = g_ext4.blocks_count;
        else
            g_ext4.free_blocks += chunk;
        flush_bitmap();

        first += chunk;
        count -= chunk;
    }
}

int ext4_alloc_inode(u32 *ino)
{
    if (!g_ext4.writable || !ino)
        return -1;
    for (u32 group = 0; group < g_ext4.groups; group++) {
        u8 desc[64];
        if (ext4_read_group_desc(group, desc) != 0)
            return -1;
        u32 bitmap_block = gd_inode_bitmap(desc);
        if (bitmap_load(bitmap_block) != 0)
            return -1;
        for (u32 index = 0; index < g_ext4.inodes_per_group; index++) {
            u32 number = group * g_ext4.inodes_per_group + index + 1;
            if (number > g_ext4.inodes_count)
                break;
            if (bitmap_test(index))
                continue;
            bitmap_set(index);
            u16 group_free = gd_free_inodes(desc);
            if (group_free == 0)
                return -1;
            gd_set_free_inodes(desc, (u16)(group_free - 1));
            if (ext4_write_group_desc(group, desc) != 0)
                return -1;
            if (g_ext4.free_inodes)
                g_ext4.free_inodes--;
            flush_bitmap();
            u8 zero[256];
            memset(zero, 0, sizeof(zero));
            if (ext4_write_inode_raw(number, zero) != 0)
                return -1;
            *ino = number;
            return 0;
        }
    }
    return -1;
}

void ext4_free_inode(u32 ino)
{
    if (!g_ext4.writable || ino < 1 || ino > g_ext4.inodes_count)
        return;
    u32 group = (ino - 1) / g_ext4.inodes_per_group;
    u32 index = (ino - 1) % g_ext4.inodes_per_group;
    u8 desc[64];
    if (ext4_read_group_desc(group, desc) != 0)
        return;
    if (bitmap_load(gd_inode_bitmap(desc)) != 0)
        return;
    if (!bitmap_test(index))
        return;
    bitmap_clear(index);
    gd_set_free_inodes(desc, (u16)(gd_free_inodes(desc) + 1));
    if (ext4_write_group_desc(group, desc) != 0)
        return;
    if (g_ext4.free_inodes < g_ext4.inodes_count)
        g_ext4.free_inodes++;
    flush_bitmap();
}

int ext4_write_inode_raw(u32 ino, const u8 *inode)
{
    if (!g_ext4.writable || ino == 0 || ino > g_ext4.inodes_count)
        return -1;
    u32 group = (ino - 1) / g_ext4.inodes_per_group;
    u32 index = (ino - 1) % g_ext4.inodes_per_group;
    u8 desc[64];
    if (ext4_read_group_desc(group, desc) != 0)
        return -1;
    u64 offset = (u64)ext4_gd_inode_table(desc) * g_ext4.block_size +
                 (u64)index * g_ext4.inode_size;
    u64 block = offset / g_ext4.block_size;
    u32 within = (u32)(offset % g_ext4.block_size);
    u32 bytes = g_ext4.inode_size < 256 ? g_ext4.inode_size : 256;
    u8 *buffer = ext4_alloc(g_ext4.block_size);
    if (!buffer)
        return -1;
    int result = ext4_read_block(block, buffer);
    if (result == 0) {
        memcpy(buffer + within, inode, bytes);
        result = ext4_write_block(block, buffer);
    }
    ext4_free(buffer);
    return result;
}

int inode_is_dir(const u8 *inode)
{
    return (ext4_le16(inode) & EXT4_S_IFDIR) != 0;
}

u32 inode_size(const u8 *inode)
{
    u64 value = ext4_le32(inode + EXT4_INODE_SIZE_OFF);
    if (g_ext4.feature_ro_compat & 0x2)
        value |= (u64)ext4_le32(inode + 0x6C) << 32;
    return (u32)value;
}

void inode_set_size(u8 *inode, u32 size)
{
    ext4_put32(inode + EXT4_INODE_SIZE_OFF, size);
    ext4_put32(inode + 0x6C, 0);
}

void inode_init_extents(u8 *inode)
{
    ext4_put32(inode + EXT4_INODE_FLAGS_OFF, EXT4_EXTENTS_FL);
    u8 *node = inode + EXT4_INODE_BLOCK_OFF;
    memset(node, 0, 60);
    ext4_put16(node, EXT4_EXTENT_MAGIC);
    ext4_put16(node + 2, 0);
    ext4_put16(node + 4, 4);
    ext4_put16(node + 6, 0);
    ext4_put32(node + 8, 0);
}

int inode_extent_append(u8 *inode, u32 logical, u32 first, u32 count)
{
    if (!(ext4_le32(inode + EXT4_INODE_FLAGS_OFF) & EXT4_EXTENTS_FL))
        return -1;
    u8 *node = inode + EXT4_INODE_BLOCK_OFF;
    if (ext4_le16(node) != EXT4_EXTENT_MAGIC || ext4_le16(node + 6) != 0)
        return -1;
    u16 entries = ext4_le16(node + 2);
    u16 max = ext4_le16(node + 4);
    if (entries >= max || entries >= 4)
        return -1;
    u8 *entry = node + 12 + (u32)entries * 12;
    ext4_put32(entry, logical);
    ext4_put16(entry + 4, (u16)count);
    ext4_put16(entry + 6, 0);
    ext4_put32(entry + 8, first);
    ext4_put16(node + 2, (u16)(entries + 1));
    return 0;
}

void inode_clear_extents(u8 *inode)
{
    ext4_put32(inode + EXT4_INODE_FLAGS_OFF, EXT4_EXTENTS_FL);
    u8 *node = inode + EXT4_INODE_BLOCK_OFF;
    memset(node, 0, 60);
    ext4_put16(node, EXT4_EXTENT_MAGIC);
    ext4_put16(node + 2, 0);
    ext4_put16(node + 4, 4);
    ext4_put16(node + 6, 0);
    ext4_put32(node + 8, 0);
    ext4_put32(inode + EXT4_INODE_BLOCKS_OFF, 0);
    inode_set_size(inode, 0);
}

int ext4_truncate(u32 ino)
{
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    u8 *node = inode + EXT4_INODE_BLOCK_OFF;
    if (ext4_le16(node) != EXT4_EXTENT_MAGIC || ext4_le16(node + 6) != 0)
        return -1;
    u16 entries = ext4_le16(node + 2);
    if (entries > 4)
        return -1;
    for (u16 i = 0; i < entries; i++) {
        const u8 *entry = node + 12 + (u32)i * 12;
        u16 length = ext4_le16(entry + 4);
        if (length > 0x8000)
            length = (u16)(length - 0x8000);
        u64 first = ((u64)ext4_le16(entry + 6) << 32) | ext4_le32(entry + 8);
        if (length)
            ext4_free_run((u32)first, length);
    }
    inode_clear_extents(inode);
    return ext4_write_inode_raw(ino, inode);
}

/* Post-mount byte access, used where a transfer spans several blocks. */
static int disk_read_bytes(u64 offset, void *buffer, u32 length)
{
    const struct ext4_env *env = ext4_env();
    if (!g_ext4.mounted || !env->read)
        return -1;
    return env->read(g_ext4.disk, g_ext4.volume_base, offset, buffer, length);
}

static int disk_write_bytes(u64 offset, const void *buffer, u32 length)
{
    const struct ext4_env *env = ext4_env();
    if (!g_ext4.mounted || !g_ext4.writable || !env->write)
        return -1;
    return env->write(g_ext4.disk, g_ext4.volume_base, offset, buffer, length);
}

int ext4_update_super(void)
{
    if (!g_ext4.mounted)
        return -1;
    u8 sb[1024];
    if (disk_read_bytes(EXT4_SB_OFFSET, sb, sizeof(sb)) != 0)
        return -1;
    ext4_put32(sb + 0x0C, g_ext4.free_blocks);
    ext4_put32(sb + 0x10, g_ext4.free_inodes);
    ext4_put32(sb + 0x30, 0);
    ext4_put32(sb + 0x150, 0);
    ext4_put32(sb + 0x158, 0);
    ext4_put32(sb + 0x160, 0);

    u32 gdt_blocks = (g_ext4.groups * g_ext4.desc_size +
                      g_ext4.block_size - 1) / g_ext4.block_size;
    u8 *gdt = ext4_alloc(gdt_blocks * g_ext4.block_size);
    u8 *block = ext4_alloc(g_ext4.block_size);
    if (!gdt || !block) {
        ext4_free(gdt);
        ext4_free(block);
        return -1;
    }
    if (disk_read_bytes((u64)g_ext4.gdt_block * g_ext4.block_size, gdt,
                        gdt_blocks * g_ext4.block_size) != 0) {
        ext4_free(gdt);
        ext4_free(block);
        return -1;
    }

    /* primary superblock lives at byte 1024 of block 0 */
    memset(block, 0, g_ext4.block_size);
    memcpy(block + 1024, sb, 1024);
    if (disk_write_bytes(0, block, g_ext4.block_size) != 0) {
        ext4_free(gdt);
        ext4_free(block);
        return -1;
    }

    if (disk_write_bytes((u64)g_ext4.gdt_block * g_ext4.block_size, gdt,
                         gdt_blocks * g_ext4.block_size) != 0) {
        ext4_free(gdt);
        ext4_free(block);
        return -1;
    }

    for (u32 group = 1; group < g_ext4.groups; group++) {
        if (!fs_has_super(group))
            continue;
        u64 group_block = (u64)group * g_ext4.blocks_per_group;
        memcpy(block, sb, 1024);
        memset(block + 1024, 0, g_ext4.block_size - 1024);
        ext4_put16(block + 0x5A, (u16)group);
        if (disk_write_bytes(group_block * g_ext4.block_size, block,
                             g_ext4.block_size) != 0)
            break;
        if (disk_write_bytes((group_block + 1) * g_ext4.block_size, gdt,
                             gdt_blocks * g_ext4.block_size) != 0)
            break;
    }

    ext4_free(gdt);
    ext4_free(block);
    return 0;
}

int ext4_sync(void)
{
    if (!g_ext4.writable)
        return -1;
    ext4_bitmap_invalidate();
    if (ext4_update_super() != 0)
        return -1;
    const struct ext4_env *env = ext4_env();
    if (!env->flush)
        return 0;
    return env->flush(g_ext4.disk) == 0 ? 0 : -1;
}
