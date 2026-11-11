/*
 * Zhaoxu OS - ext4 write side and formatter.
 *
 * The installer formats a 4 KiB-block ext4 volume with extents, filetype
 * directories and sparse superblocks, but no journal and no metadata
 * checksums.  Files are allocated as one contiguous extent, which keeps the
 * legacy BIOS boot path able to load the kernel with plain sector reads.
 */
#include "ext4_internal.h"

#include <block.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>

#define EXT4_BS_MIN 1024u
#define EXT4_BPG 32768u
#define EXT4_IPG 8192u
#define EXT4_ISIZE 256u

static int fs_has_super(u32 group);

static u8 *g_bitmap;
static u32 g_bitmap_block;
static int g_bitmap_valid;
static int g_bitmap_dirty;

static u16 le16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u32 align4(u32 value)
{
    return (value + 3u) & ~3u;
}

static void flush_bitmap(void)
{
    if (g_bitmap_valid && g_bitmap_dirty && g_ext4.writable)
        ext4_write_block(g_bitmap_block, g_bitmap);
    g_bitmap_dirty = 0;
}

static void bitmap_invalidate(void)
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
        g_bitmap = kmalloc(g_ext4.block_size);
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
    u64 value = le32(desc);
    if (g_ext4.desc_size >= 64)
        value |= (u64)le32(desc + 0x20) << 32;
    return (u32)value;
}

static u32 gd_inode_bitmap(const u8 *desc)
{
    u64 value = le32(desc + 4);
    if (g_ext4.desc_size >= 64)
        value |= (u64)le32(desc + 0x24) << 32;
    return (u32)value;
}

static u32 gd_inode_table(const u8 *desc)
{
    u64 value = le32(desc + 8);
    if (g_ext4.desc_size >= 64)
        value |= (u64)le32(desc + 0x28) << 32;
    return (u32)value;
}

static u16 gd_free_blocks(const u8 *desc)
{
    return le16(desc + 0x0C);
}

static void gd_set_free_blocks(u8 *desc, u16 value)
{
    ext4_put16(desc + 0x0C, value);
}

static u16 gd_free_inodes(const u8 *desc)
{
    return le16(desc + 0x0E);
}

static void gd_set_free_inodes(u8 *desc, u16 value)
{
    ext4_put16(desc + 0x0E, value);
}

static u16 gd_used_dirs(const u8 *desc)
{
    return le16(desc + 0x10);
}

static void gd_set_used_dirs(u8 *desc, u16 value)
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
    u8 *buffer = kmalloc(g_ext4.block_size);
    if (!buffer)
        return -1;
    int result = ext4_read_block(block, buffer);
    if (result == 0) {
        memcpy(buffer + within, desc, g_ext4.desc_size);
        result = ext4_write_block(block, buffer);
    }
    kfree(buffer);
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

static int fmt_has_super(u32 group)
{
    if (group == 0 || group == 1)
        return 1;
    u32 power = 3;
    for (int base = 3; base <= 7; base += 2) {
        power = (u32)base;
        while (power < group)
            power *= (u32)base;
        if (power == group)
            return 1;
    }
    return 0;
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
    u64 offset = (u64)gd_inode_table(desc) * g_ext4.block_size +
                 (u64)index * g_ext4.inode_size;
    u64 block = offset / g_ext4.block_size;
    u32 within = (u32)(offset % g_ext4.block_size);
    u32 bytes = g_ext4.inode_size < 256 ? g_ext4.inode_size : 256;
    u8 *buffer = kmalloc(g_ext4.block_size);
    if (!buffer)
        return -1;
    int result = ext4_read_block(block, buffer);
    if (result == 0) {
        memcpy(buffer + within, inode, bytes);
        result = ext4_write_block(block, buffer);
    }
    kfree(buffer);
    return result;
}

static int inode_is_dir(const u8 *inode)
{
    return (le16(inode) & EXT4_S_IFDIR) != 0;
}

static u32 inode_size(const u8 *inode)
{
    u64 value = le32(inode + 0x04);
    if (g_ext4.feature_ro_compat & 0x2)
        value |= (u64)le32(inode + 0x6C) << 32;
    return (u32)value;
}

static void inode_set_size(u8 *inode, u32 size)
{
    ext4_put32(inode + 0x04, size);
    ext4_put32(inode + 0x6C, 0);
}

static void inode_init_extents(u8 *inode)
{
    ext4_put32(inode + 0x20, EXT4_EXTENTS_FL);
    u8 *node = inode + 0x28;
    memset(node, 0, 60);
    ext4_put16(node, EXT4_EXTENT_MAGIC);
    ext4_put16(node + 2, 0);
    ext4_put16(node + 4, 4);
    ext4_put16(node + 6, 0);
    ext4_put32(node + 8, 0);
}

static int inode_extent_append(u8 *inode, u32 logical, u32 first, u32 count)
{
    if (!(le32(inode + 0x20) & EXT4_EXTENTS_FL))
        return -1;
    u8 *node = inode + 0x28;
    if (le16(node) != EXT4_EXTENT_MAGIC || le16(node + 6) != 0)
        return -1;
    u16 entries = le16(node + 2);
    u16 max = le16(node + 4);
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

static void inode_clear_extents(u8 *inode)
{
    ext4_put32(inode + 0x20, EXT4_EXTENTS_FL);
    u8 *node = inode + 0x28;
    memset(node, 0, 60);
    ext4_put16(node, EXT4_EXTENT_MAGIC);
    ext4_put16(node + 2, 0);
    ext4_put16(node + 4, 4);
    ext4_put16(node + 6, 0);
    ext4_put32(node + 8, 0);
    ext4_put32(inode + 0x1C, 0);
    inode_set_size(inode, 0);
}

int ext4_truncate(u32 ino)
{
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    u8 *node = inode + 0x28;
    if (le16(node) != EXT4_EXTENT_MAGIC || le16(node + 6) != 0)
        return -1;
    u16 entries = le16(node + 2);
    if (entries > 4)
        return -1;
    for (u16 i = 0; i < entries; i++) {
        const u8 *entry = node + 12 + (u32)i * 12;
        u16 length = le16(entry + 4);
        if (length > 0x8000)
            length = (u16)(length - 0x8000);
        u64 first = ((u64)le16(entry + 6) << 32) | le32(entry + 8);
        if (length)
            ext4_free_run((u32)first, length);
    }
    inode_clear_extents(inode);
    return ext4_write_inode_raw(ino, inode);
}

int ext4_update_super(void)
{
    if (!g_ext4.mounted)
        return -1;
    u8 sb[1024];
    if (block_disk_read(g_ext4.disk, g_ext4.volume_base + 2, 2, sb) != 0)
        return -1;
    ext4_put32(sb + 0x0C, g_ext4.free_blocks);
    ext4_put32(sb + 0x10, g_ext4.free_inodes);
    ext4_put32(sb + 0x30, 0);
    ext4_put32(sb + 0x150, 0);
    ext4_put32(sb + 0x158, 0);
    ext4_put32(sb + 0x160, 0);

    u32 gdt_bytes = g_ext4.groups * g_ext4.desc_size;
    u32 gdt_blocks = (gdt_bytes + g_ext4.block_size - 1) /
                     g_ext4.block_size;
    u8 *gdt = kmalloc(gdt_blocks * g_ext4.block_size);
    u8 *block = kmalloc(g_ext4.block_size);
    if (!gdt || !block) {
        kfree(gdt);
        kfree(block);
        return -1;
    }
    if (block_disk_read(g_ext4.disk,
                        g_ext4.volume_base +
                            (u64)g_ext4.gdt_block * g_ext4.sectors_per_block,
                        gdt_blocks * g_ext4.sectors_per_block, gdt) != 0) {
        kfree(gdt);
        kfree(block);
        return -1;
    }

    /* primary superblock lives at byte 1024 of block 0 */
    memset(block, 0, g_ext4.block_size);
    memcpy(block + 1024, sb, 1024);
    if (block_disk_write(g_ext4.disk, g_ext4.volume_base,
                         g_ext4.sectors_per_block, block) != 0) {
        kfree(gdt);
        kfree(block);
        return -1;
    }

    u64 gdt_lba = g_ext4.volume_base +
                  (u64)g_ext4.gdt_block * g_ext4.sectors_per_block;
    if (block_disk_write(g_ext4.disk, gdt_lba,
                         gdt_blocks * g_ext4.sectors_per_block, gdt) != 0) {
        kfree(gdt);
        kfree(block);
        return -1;
    }

    for (u32 group = 1; group < g_ext4.groups; group++) {
        if (!fs_has_super(group))
            continue;
        u64 group_block = (u64)group * g_ext4.blocks_per_group;
        memcpy(block, sb, 1024);
        memset(block + 1024, 0, g_ext4.block_size - 1024);
        ext4_put16(block + 0x5A, (u16)group);
        if (block_disk_write(g_ext4.disk,
                             g_ext4.volume_base +
                                 group_block * g_ext4.sectors_per_block,
                             g_ext4.sectors_per_block, block) != 0)
            break;
        if (block_disk_write(g_ext4.disk,
                             g_ext4.volume_base +
                                 (group_block + 1) *
                                     g_ext4.sectors_per_block,
                             gdt_blocks * g_ext4.sectors_per_block, gdt) != 0)
            break;
    }

    kfree(gdt);
    kfree(block);
    return 0;
}

int ext4_sync(void)
{
    if (!g_ext4.writable)
        return -1;
    bitmap_invalidate();
    if (ext4_update_super() != 0)
        return -1;
    return block_disk_flush(g_ext4.disk) == 0 ? 0 : -1;
}

static int read_dir_block(const u8 *dir_inode, u32 logical, u8 *buffer)
{
    struct ext4_extent_path path;
    if (ext4_map_extent(dir_inode, logical, &path) != 0)
        return -1;
    return ext4_read_block(path.start, buffer);
}

static int write_dir_block(const u8 *dir_inode, u32 logical,
                           const u8 *buffer)
{
    struct ext4_extent_path path;
    if (ext4_map_extent(dir_inode, logical, &path) != 0)
        return -1;
    return ext4_write_block(path.start, buffer);
}

int ext4_dir_add(u32 parent, const char *name, u32 child, u8 type)
{
    if (!name || !name[0])
        return -1;
    u32 name_len = (u32)strlen(name);
    if (name_len > 255)
        return -1;
    u32 needed = 8 + align4(name_len);

    u8 inode[256];
    if (ext4_read_inode_raw(parent, inode) != 0 || !inode_is_dir(inode))
        return -1;

    u32 size = inode_size(inode);
    u8 *block = kmalloc(g_ext4.block_size);
    if (!block)
        return -1;

    for (u32 logical = 0; (u64)logical * g_ext4.block_size < size;
         logical++) {
        if (read_dir_block(inode, logical, block) != 0)
            break;
        u32 offset = 0;
        while (offset + 8 <= g_ext4.block_size) {
            u8 *entry = block + offset;
            u32 rec = le16(entry + 4);
            if (rec < 8 || offset + rec > g_ext4.block_size)
                break;
            u32 used = 0;
            if (le32(entry) != 0) {
                u32 existing = entry[6];
                used = 8 + align4(existing);
                if (existing == name_len &&
                    memcmp(entry + 8, name, name_len) == 0) {
                    kfree(block);
                    return -1;
                }
            }
            if (used == 0) {
                if (rec >= needed) {
                    memset(entry, 0, rec);
                    ext4_put32(entry, child);
                    ext4_put16(entry + 4, (u16)rec);
                    entry[6] = (u8)name_len;
                    entry[7] = type;
                    memcpy(entry + 8, name, name_len);
                    int result = write_dir_block(inode, logical, block);
                    kfree(block);
                    return result;
                }
            } else if (rec - used >= needed) {
                ext4_put16(entry + 4, (u16)used);
                u8 *fresh = entry + used;
                u32 fresh_rec = rec - used;
                memset(fresh, 0, fresh_rec);
                ext4_put32(fresh, child);
                ext4_put16(fresh + 4, (u16)fresh_rec);
                fresh[6] = (u8)name_len;
                fresh[7] = type;
                memcpy(fresh + 8, name, name_len);
                int result = write_dir_block(inode, logical, block);
                kfree(block);
                return result;
            }
            offset += rec;
        }
    }

    /* No room: append a fresh directory block. */
    u32 fresh_block = 0;
    if (ext4_alloc_run(1, &fresh_block) != 0) {
        kfree(block);
        return -1;
    }
    memset(block, 0, g_ext4.block_size);
    u8 *entry = block;
    ext4_put32(entry, child);
    ext4_put16(entry + 4, (u16)g_ext4.block_size);
    entry[6] = (u8)name_len;
    entry[7] = type;
    memcpy(entry + 8, name, name_len);
    if (ext4_write_block(fresh_block, block) != 0) {
        kfree(block);
        ext4_free_run(fresh_block, 1);
        return -1;
    }

    u32 logical = size / g_ext4.block_size;
    if (inode_extent_append(inode, logical, fresh_block, 1) != 0) {
        kfree(block);
        ext4_free_run(fresh_block, 1);
        return -1;
    }
    inode_set_size(inode, size + g_ext4.block_size);
    ext4_put32(inode + 0x1C, le32(inode + 0x1C) + 8);
    int result = ext4_write_inode_raw(parent, inode);
    kfree(block);
    return result;
}

int ext4_dir_remove(u32 parent, const char *name)
{
    if (!name || !name[0])
        return -1;
    u32 name_len = (u32)strlen(name);
    u8 inode[256];
    if (ext4_read_inode_raw(parent, inode) != 0 || !inode_is_dir(inode))
        return -1;

    u32 size = inode_size(inode);
    u8 *block = kmalloc(g_ext4.block_size);
    if (!block)
        return -1;

    for (u32 logical = 0; (u64)logical * g_ext4.block_size < size;
         logical++) {
        if (read_dir_block(inode, logical, block) != 0)
            break;
        u32 offset = 0;
        u32 previous = 0xFFFFFFFFu;
        while (offset + 8 <= g_ext4.block_size) {
            u8 *entry = block + offset;
            u32 rec = le16(entry + 4);
            if (rec < 8 || offset + rec > g_ext4.block_size)
                break;
            u32 existing = le32(entry) != 0 ? entry[6] : 0;
            if (existing == name_len &&
                memcmp(entry + 8, name, name_len) == 0) {
                if (previous != 0xFFFFFFFFu) {
                    u8 *prev = block + previous;
                    ext4_put16(prev + 4,
                               (u16)(le16(prev + 4) + rec));
                } else {
                    ext4_put32(entry, 0);
                }
                int result = write_dir_block(inode, logical, block);
                kfree(block);
                return result;
            }
            previous = offset;
            offset += rec;
        }
    }

    kfree(block);
    return -1;
}

int ext4_create_node(u32 parent, const char *name, u16 mode, u8 type,
                     u32 *ino_out)
{
    if (!name || !name[0])
        return -1;
    u32 ino = 0;
    if (ext4_alloc_inode(&ino) != 0)
        return -1;

    u8 inode[256];
    memset(inode, 0, sizeof(inode));
    ext4_put16(inode, mode);
    ext4_put16(inode + 0x1A, (u16)(type == EXT4_FT_DIR ? 2 : 1));
    inode_init_extents(inode);
    ext4_put16(inode + 0x80, 32);

    if (type == EXT4_FT_DIR) {
        u32 block = 0;
        if (ext4_alloc_run(1, &block) != 0) {
            ext4_free_inode(ino);
            return -1;
        }
        u8 *data = kmalloc(g_ext4.block_size);
        if (!data) {
            ext4_free_run(block, 1);
            ext4_free_inode(ino);
            return -1;
        }
        memset(data, 0, g_ext4.block_size);
        u8 *dot = data;
        ext4_put32(dot, ino);
        ext4_put16(dot + 4, 12);
        dot[6] = 1;
        dot[7] = EXT4_FT_DIR;
        dot[8] = '.';
        u8 *dotdot = data + 12;
        ext4_put32(dotdot, parent);
        ext4_put16(dotdot + 4, (u16)(g_ext4.block_size - 12));
        dotdot[6] = 2;
        dotdot[7] = EXT4_FT_DIR;
        dotdot[8] = '.';
        dotdot[9] = '.';
        int result = ext4_write_block(block, data);
        kfree(data);
        if (result != 0) {
            ext4_free_run(block, 1);
            ext4_free_inode(ino);
            return -1;
        }
        if (inode_extent_append(inode, 0, block, 1) != 0) {
            ext4_free_run(block, 1);
            ext4_free_inode(ino);
            return -1;
        }
        inode_set_size(inode, g_ext4.block_size);
        ext4_put32(inode + 0x1C, 8);

        u8 parent_inode[256];
        if (ext4_read_inode_raw(parent, parent_inode) == 0) {
            ext4_put16(parent_inode + 0x1A,
                       (u16)(le16(parent_inode + 0x1A) + 1));
            ext4_write_inode_raw(parent, parent_inode);
        }
        u8 desc[64];
        u32 group = (ino - 1) / g_ext4.inodes_per_group;
        if (ext4_read_group_desc(group, desc) == 0) {
            gd_set_used_dirs(desc, (u16)(gd_used_dirs(desc) + 1));
            ext4_write_group_desc(group, desc);
        }
    }

    if (ext4_write_inode_raw(ino, inode) != 0 ||
        ext4_dir_add(parent, name, ino, type) != 0) {
        ext4_free_inode(ino);
        return -1;
    }
    if (ino_out)
        *ino_out = ino;
    return 0;
}

static int split_parent(const char *path, char *parent, char *name)
{
    if (!path || path[0] != '/')
        return -1;
    usize length = (usize)strlen(path);
    while (length > 1 && path[length - 1] == '/')
        length--;
    if (length <= 1)
        return -1;

    usize slash = length;
    while (slash > 0 && path[slash - 1] != '/')
        slash--;
    if (slash == 0)
        return -1;

    usize name_len = length - slash;
    if (name_len == 0 || name_len >= 256)
        return -1;
    memcpy(name, path + slash, name_len);
    name[name_len] = 0;

    if (slash == 1) {
        parent[0] = '/';
        parent[1] = 0;
    } else {
        memcpy(parent, path, slash - 1);
        parent[slash - 1] = 0;
    }
    return 0;
}

static int lookup_child(u32 parent, const char *name, u32 *child, u8 *type)
{
    u8 inode[256];
    if (ext4_read_inode_raw(parent, inode) != 0)
        return -1;
    return ext4_inode_dir_find(inode, name, child, type);
}

int ext4_write_file(const char *path, const u8 *data, u64 size)
{
    if (!g_ext4.writable || !path || size > EXT4_MAX_FILE_SIZE)
        return -1;
    if (size && !data)
        return -1;

    char parent_path[256];
    char name[256];
    if (split_parent(path, parent_path, name) != 0) {
        kprintf("[ext4] split failed\n");
        return -1;
    }
    u32 parent = 0;
    if (ext4_lookup(parent_path, &parent) != 0) {
        kprintf("[ext4] parent %s lookup failed\n", parent_path);
        return -1;
    }

    u32 existing = 0;
    u8 type = 0;
    int found = lookup_child(parent, name, &existing, &type) == 0;
    if (found && type == EXT4_FT_DIR)
        return -1;

    u32 ino = 0;
    int fresh = !found;
    if (fresh) {
        if (ext4_alloc_inode(&ino) != 0) {
            kprintf("[ext4] alloc inode failed\n");
            return -1;
        }
    } else {
        ino = existing;
    }

    if (found && ext4_truncate(ino) != 0) {
        if (fresh)
            ext4_free_inode(ino);
        return -1;
    }

    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0) {
        if (fresh)
            ext4_free_inode(ino);
        return -1;
    }
    if (fresh) {
        memset(inode, 0, sizeof(inode));
        ext4_put16(inode, (u16)(EXT4_S_IFREG | 0644));
        ext4_put16(inode + 0x1A, 1);
        inode_init_extents(inode);
        ext4_put16(inode + 0x80, 32);
    }

    u32 blocks = (u32)((size + g_ext4.block_size - 1) /
                       g_ext4.block_size);
    u32 first = 0;
    if (blocks) {
        if (ext4_alloc_run(blocks, &first) != 0) {
            kprintf("[ext4] alloc run %u failed\n", blocks);
            if (fresh)
                ext4_free_inode(ino);
            return -1;
        }
        u8 *buffer = kmalloc(g_ext4.block_size);
        if (!buffer) {
            ext4_free_run(first, blocks);
            if (fresh)
                ext4_free_inode(ino);
            return -1;
        }
        for (u32 i = 0; i < blocks; i++) {
            memset(buffer, 0, g_ext4.block_size);
            u64 offset = (u64)i * g_ext4.block_size;
            u64 chunk = size - offset;
            if (chunk > g_ext4.block_size)
                chunk = g_ext4.block_size;
            memcpy(buffer, data + offset, (usize)chunk);
            if (ext4_write_block(first + i, buffer) != 0) {
                kprintf("[ext4] data block %u write failed\n", first + i);
                kfree(buffer);
                ext4_free_run(first, blocks);
                if (fresh)
                    ext4_free_inode(ino);
                return -1;
            }
        }
        kfree(buffer);
        if (inode_extent_append(inode, 0, first, blocks) != 0) {
            kprintf("[ext4] extent append failed\n");
            ext4_free_run(first, blocks);
            if (fresh)
                ext4_free_inode(ino);
            return -1;
        }
        ext4_put32(inode + 0x1C, blocks * 8u);
    } else {
        inode_clear_extents(inode);
    }
    inode_set_size(inode, (u32)size);

    if (ext4_write_inode_raw(ino, inode) != 0) {
        kprintf("[ext4] inode write failed\n");
        if (blocks)
            ext4_free_run(first, blocks);
        if (fresh)
            ext4_free_inode(ino);
        return -1;
    }
    if (fresh && ext4_dir_add(parent, name, ino, EXT4_FT_REG_FILE) != 0) {
        kprintf("[ext4] dir add failed\n");
        ext4_free_inode(ino);
        return -1;
    }
    return ext4_sync();
}

int ext4_unlink(const char *path)
{
    if (!g_ext4.writable)
        return -1;
    char parent_path[256];
    char name[256];
    if (split_parent(path, parent_path, name) != 0)
        return -1;
    u32 parent = 0;
    if (ext4_lookup(parent_path, &parent) != 0)
        return -1;

    u32 ino = 0;
    u8 type = 0;
    if (lookup_child(parent, name, &ino, &type) != 0)
        return -1;
    if (type == EXT4_FT_DIR)
        return -1;

    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    if (ext4_truncate(ino) != 0)
        return -1;
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    ext4_put16(inode + 0x1A, 0);
    ext4_put32(inode + 0x14, 1);
    ext4_write_inode_raw(ino, inode);
    if (ext4_dir_remove(parent, name) != 0)
        return -1;
    ext4_free_inode(ino);
    return ext4_sync();
}

/* ------------------------------------------------------------------ */
/* formatter                                                           */
/* ------------------------------------------------------------------ */

static void put_inode(u8 *inode, u16 mode, u16 links, u32 size,
                      u32 block_count, u32 first, u32 count)
{
    memset(inode, 0, 256);
    ext4_put16(inode, mode);
    ext4_put16(inode + 0x1A, links);
    ext4_put32(inode + 0x1C, block_count);
    ext4_put32(inode + 0x20, EXT4_EXTENTS_FL);
    u8 *node = inode + 0x28;
    ext4_put16(node, EXT4_EXTENT_MAGIC);
    ext4_put16(node + 2, 1);
    ext4_put16(node + 4, 4);
    ext4_put16(node + 6, 0);
    ext4_put32(node + 8, 0);
    u8 *entry = node + 12;
    ext4_put32(entry, 0);
    ext4_put16(entry + 4, (u16)count);
    ext4_put16(entry + 6, 0);
    ext4_put32(entry + 8, first);
    ext4_put32(inode + 0x04, size);
    ext4_put32(inode + 0x6C, 0);
    ext4_put16(inode + 0x80, 32);
}

static int zero_sectors(int disk, u64 lba, u64 count)
{
    u32 chunk_sectors = 128;
    u8 *zero = kmalloc(chunk_sectors * 512);
    if (!zero)
        return -1;
    memset(zero, 0, chunk_sectors * 512);
    int result = 0;
    while (count) {
        u32 chunk = count > chunk_sectors ? chunk_sectors : (u32)count;
        if (block_disk_write(disk, lba, chunk, zero) != 0) {
            result = -1;
            break;
        }
        lba += chunk;
        count -= chunk;
    }
    kfree(zero);
    return result;
}

static int fs_has_super(u32 group)
{
    if (group == 0)
        return 1;
    if (!(g_ext4.feature_ro_compat & EXT4_RO_SPARSE_SUPER))
        return 1;
    return fmt_has_super(group);
}

static int group_inode_blocks(u32 group, u32 gdt_blocks, u32 itable_blocks,
                              u32 *bitmap_block, u32 *inode_bitmap_block,
                              u32 *inode_table_block, u32 *used_blocks)
{
    u64 start = (u64)group * EXT4_BPG;
    u32 cursor = 0;
    if (fmt_has_super(group))
        cursor += 1 + gdt_blocks;
    *bitmap_block = (u32)start + cursor;
    cursor++;
    *inode_bitmap_block = (u32)start + cursor;
    cursor++;
    *inode_table_block = (u32)start + cursor;
    cursor += itable_blocks;
    *used_blocks = cursor;
    return 0;
}

int ext4_format_volume(int disk, u64 volume_base, u32 sectors,
                       const char *label)
{
    u32 block_size = 4096;
    u32 spb = block_size / 512;
    u64 total_blocks = (u64)sectors * 512 / block_size;
    if (total_blocks < 512 || total_blocks > 0xFFFFFFFFull)
        return -1;
    u32 blocks = (u32)total_blocks;
    u32 groups = (blocks + EXT4_BPG - 1) / EXT4_BPG;
    if (groups == 0)
        return -1;
    u32 gdt_bytes = groups * 32;
    u32 gdt_blocks = (gdt_bytes + block_size - 1) / block_size;
    u32 itable_blocks = EXT4_IPG * EXT4_ISIZE / block_size;

    u8 *gdt = kmalloc(gdt_blocks * block_size);
    if (!gdt)
        return -1;
    memset(gdt, 0, gdt_blocks * block_size);

    u32 free_blocks = 0;
    u32 free_inodes = groups * EXT4_IPG;

    for (u32 group = 0; group < groups; group++) {
        u64 start64 = (u64)group * EXT4_BPG;
        if (start64 >= blocks)
            break;
        u32 in_group = blocks - (u32)start64;
        if (in_group > EXT4_BPG)
            in_group = EXT4_BPG;

        u32 bitmap_block, inode_bitmap_block, inode_table_block, used;
        group_inode_blocks(group, gdt_blocks, itable_blocks, &bitmap_block,
                           &inode_bitmap_block, &inode_table_block, &used);
        if (used > in_group) {
            kfree(gdt);
            return -1;
        }

        u32 reserved = group == 0 ? EXT4_LOST_FOUND_INO : 0;
        u32 group_free_blocks = in_group - used;
        u32 group_free_inodes = EXT4_IPG - reserved;
        free_blocks += group_free_blocks;
        free_inodes -= reserved;

        u8 *desc = gdt + (u64)group * 32;
        ext4_put32(desc, bitmap_block);
        ext4_put32(desc + 4, inode_bitmap_block);
        ext4_put32(desc + 8, inode_table_block);
        ext4_put16(desc + 0x0C, (u16)group_free_blocks);
        ext4_put16(desc + 0x0E, (u16)group_free_inodes);
        ext4_put16(desc + 0x10, (u16)(group == 0 ? 2 : 0));
    }

    /* Group 0 needs two data blocks for the root and lost+found dirs. */
    u32 bitmap_block0, inode_bitmap0, inode_table0, used0;
    group_inode_blocks(0, gdt_blocks, itable_blocks, &bitmap_block0,
                       &inode_bitmap0, &inode_table0, &used0);
    u32 root_block = used0;
    u32 lost_block = used0 + 1;
    u32 in_group0 = blocks > EXT4_BPG ? EXT4_BPG : blocks;
    if (lost_block >= in_group0) {
        kfree(gdt);
        return -1;
    }

    u8 *desc0 = gdt;
    ext4_put16(desc0 + 0x0C,
               (u16)(le16(desc0 + 0x0C) - 2));
    free_blocks -= 2;

    u8 *bitmap = kmalloc(block_size);
    u8 *block = kmalloc(block_size);
    u8 *sb = kmalloc(1024);
    if (!bitmap || !block || !sb) {
        kfree(gdt);
        kfree(bitmap);
        kfree(block);
        kfree(sb);
        return -1;
    }

    /* copy of the superblock */
    memset(sb, 0, 1024);
    ext4_put32(sb + 0x00, groups * EXT4_IPG);
    ext4_put32(sb + 0x04, blocks);
    ext4_put32(sb + 0x08, blocks / 20);
    ext4_put32(sb + 0x0C, free_blocks);
    ext4_put32(sb + 0x10, free_inodes);
    ext4_put32(sb + 0x14, 0);
    ext4_put32(sb + 0x18, 2);
    ext4_put32(sb + 0x1C, 2);
    ext4_put32(sb + 0x20, EXT4_BPG);
    ext4_put32(sb + 0x24, EXT4_BPG);
    ext4_put32(sb + 0x28, EXT4_IPG);
    ext4_put16(sb + 0x34, 0);
    ext4_put16(sb + 0x36, 0xFFFF);
    ext4_put16(sb + 0x38, EXT4_MAGIC);
    ext4_put16(sb + 0x3A, 1);
    ext4_put16(sb + 0x3C, 1);
    ext4_put32(sb + 0x48, 0);
    ext4_put32(sb + 0x4C, 1);
    ext4_put32(sb + 0x54, EXT4_LOST_FOUND_INO);
    ext4_put16(sb + 0x58, EXT4_ISIZE);
    ext4_put32(sb + 0x5C, 0);
    ext4_put32(sb + 0x60, EXT4_INCOMPAT_FILETYPE | EXT4_INCOMPAT_EXTENTS);
    ext4_put32(sb + 0x64, EXT4_RO_SPARSE_SUPER | EXT4_RO_EXTRA_ISIZE);
    static const u8 uuid[16] = {
        0x5A, 0x4F, 0x53, 0x45, 0x58, 0x54, 0x34, 0x00,
        0x9A, 0x4F, 0x53, 0x31, 0x00, 0x00, 0x00, 0x03
    };
    memcpy(sb + 0x68, uuid, 16);
    /* s_volume_name is 16 bytes and does not have to be NUL terminated, but
     * the label handed in is a C string: stop at its NUL instead of reading
     * up to 16 bytes out of a shorter literal.  sb is already zeroed, so the
     * on-disk bytes are unchanged for every well-formed label. */
    for (int i = 0; i < 16 && label && label[i]; i++)
        sb[0x78 + i] = (u8)label[i];
    ext4_put16(sb + 0x5A, 0);
    ext4_put16(sb + 0x15C, 32);
    ext4_put16(sb + 0x15E, 32);

    /* block bitmaps and inode bitmaps */
    for (u32 group = 0; group < groups; group++) {
        u64 start64 = (u64)group * EXT4_BPG;
        u32 in_group = blocks > (u32)start64 ? blocks - (u32)start64 : 0;
        if (in_group > EXT4_BPG)
            in_group = EXT4_BPG;
        if (!in_group)
            break;

        u32 bm, ibm, itb, used;
        group_inode_blocks(group, gdt_blocks, itable_blocks, &bm, &ibm, &itb,
                           &used);
        memset(bitmap, 0, block_size);
        for (u32 i = 0; i < used; i++)
            bitmap[i / 8] |= (u8)(1u << (i % 8));
        for (u32 i = in_group; i < EXT4_BPG; i++)
            bitmap[i / 8] |= (u8)(1u << (i % 8));
        if (group == 0) {
            bitmap[root_block / 8] |= (u8)(1u << (root_block % 8));
            bitmap[lost_block / 8] |= (u8)(1u << (lost_block % 8));
        }
        if (block_disk_write(disk,
                             volume_base + (u64)bm * spb, spb, bitmap) != 0) {
            kfree(gdt);
            kfree(bitmap);
            kfree(block);
            kfree(sb);
            return -1;
        }

        memset(bitmap, 0xFF, block_size);
        for (u32 i = 0; i < EXT4_IPG; i++)
            bitmap[i / 8] &= (u8)~(1u << (i % 8));
        if (group == 0) {
            for (u32 i = 0; i < EXT4_LOST_FOUND_INO; i++)
                bitmap[i / 8] |= (u8)(1u << (i % 8));
        }
        if (block_disk_write(disk,
                             volume_base + (u64)ibm * spb, spb, bitmap) != 0) {
            kfree(gdt);
            kfree(bitmap);
            kfree(block);
            kfree(sb);
            return -1;
        }

        if (zero_sectors(disk,
                         volume_base + (u64)itb * spb,
                         (u64)itable_blocks * spb) != 0) {
            kfree(gdt);
            kfree(bitmap);
            kfree(block);
            kfree(sb);
            return -1;
        }
    }

    /* root and lost+found directory blocks */
    memset(block, 0, block_size);
    u8 *entry = block;
    ext4_put32(entry, EXT4_ROOT_INO);
    ext4_put16(entry + 4, 12);
    entry[6] = 1;
    entry[7] = EXT4_FT_DIR;
    entry[8] = '.';
    entry = block + 12;
    ext4_put32(entry, EXT4_ROOT_INO);
    ext4_put16(entry + 4, 12);
    entry[6] = 2;
    entry[7] = EXT4_FT_DIR;
    entry[8] = '.';
    entry[9] = '.';
    entry = block + 24;
    ext4_put32(entry, EXT4_LOST_FOUND_INO);
    ext4_put16(entry + 4, (u16)(block_size - 24));
    entry[6] = 10;
    entry[7] = EXT4_FT_DIR;
    memcpy(entry + 8, "lost+found", 10);
    if (block_disk_write(disk, volume_base + (u64)root_block * spb, spb,
                         block) != 0) {
        kfree(gdt);
        kfree(bitmap);
        kfree(block);
        kfree(sb);
        return -1;
    }

    memset(block, 0, block_size);
    entry = block;
    ext4_put32(entry, EXT4_LOST_FOUND_INO);
    ext4_put16(entry + 4, 12);
    entry[6] = 1;
    entry[7] = EXT4_FT_DIR;
    entry[8] = '.';
    entry = block + 12;
    ext4_put32(entry, EXT4_ROOT_INO);
    ext4_put16(entry + 4, (u16)(block_size - 12));
    entry[6] = 2;
    entry[7] = EXT4_FT_DIR;
    entry[8] = '.';
    entry[9] = '.';
    if (block_disk_write(disk, volume_base + (u64)lost_block * spb, spb,
                         block) != 0) {
        kfree(gdt);
        kfree(bitmap);
        kfree(block);
        kfree(sb);
        return -1;
    }

    /* inode table entry block (inodes 1..16) */
    memset(block, 0, block_size);
    put_inode(block + 1 * EXT4_ISIZE, (u16)(EXT4_S_IFDIR | 0755), 3,
              block_size, 8, root_block, 1);
    put_inode(block + 10 * EXT4_ISIZE, (u16)(EXT4_S_IFDIR | 0700), 2,
              block_size, 8, lost_block, 1);
    if (block_disk_write(disk,
                         volume_base + (u64)inode_table0 * spb, spb,
                         block) != 0) {
        kfree(gdt);
        kfree(bitmap);
        kfree(block);
        kfree(sb);
        return -1;
    }

    /* primary superblock + group descriptors */
    memset(block, 0, block_size);
    memcpy(block + 1024, sb, 1024);
    if (block_disk_write(disk, volume_base, spb, block) != 0) {
        kfree(gdt);
        kfree(bitmap);
        kfree(block);
        kfree(sb);
        return -1;
    }
    if (block_disk_write(disk, volume_base + (u64)1 * spb,
                         gdt_blocks * spb, gdt) != 0) {
        kfree(gdt);
        kfree(bitmap);
        kfree(block);
        kfree(sb);
        return -1;
    }

    /* backup superblocks and group descriptors */
    for (u32 group = 1; group < groups; group++) {
        if (!fmt_has_super(group))
            continue;
        u64 start = (u64)group * EXT4_BPG;
        memcpy(block, sb, 1024);
        memset(block + 1024, 0, block_size - 1024);
        ext4_put16(block + 0x5A, (u16)group);
        if (block_disk_write(disk, volume_base + start * spb, spb, block) != 0)
            break;
        if (block_disk_write(disk, volume_base + (start + 1) * spb,
                             gdt_blocks * spb, gdt) != 0)
            break;
    }

    kfree(gdt);
    kfree(bitmap);
    kfree(block);
    kfree(sb);
    return block_disk_flush(disk) == 0 ? 0 : -1;
}

int ext4_make_root(int disk, u64 volume_base, u32 sectors, const char *label,
                   const u8 *kernel_elf, u64 kernel_elf_size,
                   const u8 *kernel_bin, u64 kernel_bin_size,
                   const u8 *font, u64 font_size,
                   u32 *bin_block, u32 *bin_blocks)
{
    if (!kernel_elf || !kernel_elf_size || !kernel_bin || !kernel_bin_size)
        return -1;
    if (ext4_format_volume(disk, volume_base, sectors, label) != 0) {
        kprintf("[ext4] format failed \n");
        return -1;
    }
    if (ext4_mount_at(disk, volume_base) != 0) {
        kprintf("[ext4] mount after format failed \n");
        return -1;
    }
    if (!g_ext4.writable) {
        kprintf("[ext4] volume is read-only \n");
        return -1;
    }
    kprintf("[ext4] formatted %u blocks, %u groups, label %s \n",
            g_ext4.blocks_count, g_ext4.groups, g_ext4.label);

    if (ext4_write_file("/KERNEL.ELF", kernel_elf, kernel_elf_size) != 0) {
        kprintf("[ext4] KERNEL.ELF write failed \n");
        return -1;
    }
    if (ext4_write_file("/KERNEL.BIN", kernel_bin, kernel_bin_size) != 0) {
        kprintf("[ext4] KERNEL.BIN write failed \n");
        return -1;
    }
    if (font && font_size > 12 &&
        ext4_write_file("/WQY.TTC", font, font_size) != 0) {
        kprintf("[ext4] WQY.TTC write failed \n");
        return -1;
    }

    u32 first = 0;
    u32 count = 0;
    if (ext4_file_extent("/KERNEL.BIN", &first, &count) != 0) {
        kprintf("[ext4] KERNEL.BIN is not one extent \n");
        return -1;
    }
    if ((u64)count * g_ext4.block_size < kernel_bin_size)
        return -1;
    if (bin_block)
        *bin_block = first;
    if (bin_blocks)
        *bin_blocks = count;
    return ext4_sync();
}
