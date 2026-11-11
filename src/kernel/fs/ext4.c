/*
 * Zhaoxu OS - ext4 filesystem (read side and mount).
 *
 * Minimal ext4 with 32/64-bit group descriptors, extents and filetype
 * directory entries.  Volumes created by the installer use 4 KiB blocks
 * without a journal or metadata checksums; read-only access is also
 * attempted for other ext4 volumes that use extents.
 */
#include "ext4_internal.h"

#include <block.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>

#define EXT4_SB_OFFSET 1024u
#define EXT4_SB_SECTORS 2u
#define EXT4_INODE_BLOCK_OFF 0x28u
#define EXT4_INODE_SIZE_OFF 0x04u
#define EXT4_INODE_BLOCKS_OFF 0x1Cu
#define EXT4_INODE_FLAGS_OFF 0x20u
#define EXT4_GD_SIZE32 32u
#define EXT4_GD_SIZE64 64u

struct ext4_fs g_ext4;

static u16 le16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u64 le64(const u8 *p)
{
    return (u64)le32(p) | ((u64)le32(p + 4) << 32);
}

void ext4_put16(u8 *p, u16 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}

void ext4_put32(u8 *p, u32 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
    p[2] = (u8)(value >> 16);
    p[3] = (u8)(value >> 24);
}

int ext4_read_block(u64 block, void *buffer)
{
    if (!g_ext4.mounted)
        return -1;
    return block_disk_read(g_ext4.disk,
                           g_ext4.volume_base +
                               block * g_ext4.sectors_per_block,
                           g_ext4.sectors_per_block, buffer);
}

int ext4_write_block(u64 block, const void *buffer)
{
    if (!g_ext4.mounted || !g_ext4.writable)
        return -1;
    return block_disk_write(g_ext4.disk,
                            g_ext4.volume_base +
                                block * g_ext4.sectors_per_block,
                            g_ext4.sectors_per_block, buffer);
}

int ext4_read_group_desc(u32 group, u8 *desc)
{
    if (group >= g_ext4.groups || !desc)
        return -1;
    u64 offset = (u64)g_ext4.gdt_block * g_ext4.block_size +
                 (u64)group * g_ext4.desc_size;
    u64 block = offset / g_ext4.block_size;
    u32 within = (u32)(offset % g_ext4.block_size);
    u8 *buffer = kmalloc(g_ext4.block_size);
    if (!buffer)
        return -1;
    int result = ext4_read_block(block, buffer);
    if (result == 0)
        memcpy(desc, buffer + within, g_ext4.desc_size);
    kfree(buffer);
    return result;
}

static u32 gd_inode_table(const u8 *desc)
{
    u64 value = le32(desc + 8);
    if (g_ext4.desc_size >= EXT4_GD_SIZE64)
        value |= (u64)le32(desc + 0x28) << 32;
    return (u32)value;
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

    u64 table = gd_inode_table(desc);
    u64 offset = table * g_ext4.block_size +
                 (u64)index * g_ext4.inode_size;
    u64 block = offset / g_ext4.block_size;
    u32 within = (u32)(offset % g_ext4.block_size);
    u32 bytes = g_ext4.inode_size < 256 ? g_ext4.inode_size : 256;
    if (within + bytes > g_ext4.block_size) {
        u8 *buffer = kmalloc(g_ext4.block_size);
        if (!buffer)
            return -1;
        int result = ext4_read_block(block, buffer);
        if (result == 0)
            memcpy(inode, buffer + within, g_ext4.block_size - within);
        kfree(buffer);
        bytes -= g_ext4.block_size - within;
        if (result != 0)
            return -1;
        u8 *tail = kmalloc(g_ext4.block_size);
        if (!tail)
            return -1;
        result = ext4_read_block(block + 1, tail);
        if (result == 0)
            memcpy(inode + (g_ext4.block_size - within), tail, bytes);
        kfree(tail);
        return result;
    }

    u8 *buffer = kmalloc(g_ext4.block_size);
    if (!buffer)
        return -1;
    int result = ext4_read_block(block, buffer);
    if (result == 0)
        memcpy(inode, buffer + within, bytes);
    kfree(buffer);
    return result;
}

static int extent_lookup(const u8 *node, u16 depth, u32 logical,
                         struct ext4_extent_path *path)
{
    if (le16(node) != EXT4_EXTENT_MAGIC)
        return -1;
    u16 entries = le16(node + 2);
    if (entries > 1024)
        return -1;

    if (depth == 0) {
        for (u16 i = 0; i < entries; i++) {
            const u8 *entry = node + 12 + (u32)i * 12;
            u32 start = le32(entry);
            u16 length = le16(entry + 4);
            if (length > 0x8000)
                length = (u16)(length - 0x8000);
            if (length == 0)
                continue;
            if (logical < start || logical >= start + length)
                continue;
            u64 physical = ((u64)le16(entry + 6) << 32) | le32(entry + 8);
            path->start = physical + (logical - start);
            path->count = start + length - logical;
            return 0;
        }
        return -1;
    }

    for (u16 i = 0; i < entries; i++) {
        const u8 *entry = node + 12 + (u32)i * 12;
        u32 start = le32(entry);
        u32 next = i + 1 < entries ? le32(entry + 12) : 0xFFFFFFFFu;
        if (logical < start || logical >= next)
            continue;
        u64 leaf = ((u64)le16(entry + 8) << 32) | le32(entry + 4);
        u8 *buffer = kmalloc(g_ext4.block_size);
        if (!buffer)
            return -1;
        int result = ext4_read_block(leaf, buffer);
        if (result == 0)
            result = extent_lookup(buffer, (u16)(depth - 1), logical, path);
        kfree(buffer);
        return result;
    }
    return -1;
}

int ext4_map_extent(const u8 *inode, u32 logical,
                    struct ext4_extent_path *path)
{
    if (!path || !(le32(inode + EXT4_INODE_FLAGS_OFF) & EXT4_EXTENTS_FL))
        return -1;
    const u8 *node = inode + EXT4_INODE_BLOCK_OFF;
    if (le16(node) != EXT4_EXTENT_MAGIC)
        return -1;
    return extent_lookup(node, le16(node + 6), logical, path);
}

int ext4_read_inode_data(const u8 *inode, u64 offset, void *buffer,
                         u64 length, u64 *read_out)
{
    u64 size = le32(inode + EXT4_INODE_SIZE_OFF);
    if (g_ext4.feature_ro_compat & 0x2)
        size |= (u64)le32(inode + 0x6C) << 32;
    if (offset >= size) {
        if (read_out)
            *read_out = 0;
        return 0;
    }
    if (offset + length > size)
        length = size - offset;

    u8 *block = kmalloc(g_ext4.block_size);
    if (!block)
        return -1;
    u8 *out = buffer;
    u64 done = 0;
    int result = 0;

    while (done < length) {
        u64 position = offset + done;
        u32 logical = (u32)(position / g_ext4.block_size);
        u32 within = (u32)(position % g_ext4.block_size);
        struct ext4_extent_path path;
        if (ext4_map_extent(inode, logical, &path) != 0) {
            result = -1;
            break;
        }
        if (ext4_read_block(path.start, block) != 0) {
            result = -1;
            break;
        }
        u64 available = length - done;
        u32 chunk = g_ext4.block_size - within;
        if (available < chunk)
            chunk = (u32)available;
        memcpy(out + done, block + within, chunk);
        done += chunk;
    }

    kfree(block);
    if (read_out)
        *read_out = done;
    return result;
}

int ext4_dir_next(const u8 *block, u32 *offset,
                  struct ext4_dirent_info *info)
{
    u32 position = *offset;
    while (position + 8 <= g_ext4.block_size) {
        const u8 *entry = block + position;
        u32 inode = le32(entry);
        u16 rec_len = le16(entry + 4);
        u8 name_len = entry[6];
        u8 type = entry[7];

        if (!(g_ext4.feature_incompat & EXT4_INCOMPAT_FILETYPE)) {
            name_len = entry[6];
            type = 0;
        }

        if (rec_len < 8 || position + rec_len > g_ext4.block_size)
            return 0;
        *offset = position + rec_len;

        u32 name_size = name_len;
        if (name_size > 255)
            name_size = 255;
        if (inode != 0 && name_len != 0) {
            info->inode = inode;
            info->rec_len = rec_len;
            info->offset = position;
            info->name_len = name_len;
            info->type = type;
            memcpy(info->name, entry + 8, name_size);
            info->name[name_size] = 0;
            if (!(name_len == 1 && info->name[0] == '.') &&
                !(name_len == 2 && info->name[0] == '.' &&
                  info->name[1] == '.'))
                return 1;
        }
        position = *offset;
    }
    return 0;
}

int ext4_inode_dir_find(const u8 *inode, const char *name, u32 *ino,
                        u8 *type)
{
    u32 size = le32(inode + EXT4_INODE_SIZE_OFF);
    if (size == 0 || size > g_ext4.block_size * 1024u)
        return -1;

    u8 *block = kmalloc(g_ext4.block_size);
    if (!block)
        return -1;
    int result = -1;

    for (u32 logical = 0;
         (u64)logical * g_ext4.block_size < size && result < 0; logical++) {
        struct ext4_extent_path path;
        if (ext4_map_extent(inode, logical, &path) != 0)
            break;
        if (ext4_read_block(path.start, block) != 0)
            break;
        u32 offset = 0;
        struct ext4_dirent_info info;
        while (ext4_dir_next(block, &offset, &info) > 0) {
            if (strcmp(info.name, name) == 0) {
                *ino = info.inode;
                if (type)
                    *type = info.type;
                result = 0;
                break;
            }
        }
    }

    kfree(block);
    return result;
}

static int walk_path(const char *path, u32 *ino, u8 *type)
{
    if (!path || path[0] != '/')
        return -1;
    u32 current = EXT4_ROOT_INO;
    const char *cursor = path;
    while (*cursor == '/')
        cursor++;
    if (!*cursor) {
        *ino = current;
        if (type)
            *type = EXT4_FT_DIR;
        return 0;
    }

    char component[256];
    while (*cursor) {
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 >= sizeof(component))
                return -1;
            component[length++] = *cursor++;
        }
        component[length] = 0;
        while (*cursor == '/')
            cursor++;

        u8 inode[256];
        if (ext4_read_inode_raw(current, inode) != 0)
            return -1;
        if ((le16(inode) & EXT4_S_IFDIR) == 0)
            return -1;

        u32 next = 0;
        u8 child_type = 0;
        if (ext4_inode_dir_find(inode, component, &next, &child_type) != 0)
            return -1;
        current = next;
        if (type && !*cursor)
            *type = child_type;
    }

    *ino = current;
    return 0;
}

int ext4_lookup(const char *path, u32 *ino)
{
    return walk_path(path, ino, NULL);
}

int ext4_list(const char *path, vfs_list_cb callback, void *context)
{
    if (!g_ext4.mounted || !callback)
        return -1;

    u32 ino = 0;
    if (walk_path(path, &ino, NULL) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    if ((le16(inode) & EXT4_S_IFDIR) == 0)
        return -1;

    u32 size = le32(inode + EXT4_INODE_SIZE_OFF);
    u8 *block = kmalloc(g_ext4.block_size);
    u8 *child = kmalloc(256);
    if (!block || !child) {
        kfree(block);
        kfree(child);
        return -1;
    }

    int result = 0;
    for (u32 logical = 0; (u64)logical * g_ext4.block_size < size; logical++) {
        struct ext4_extent_path mapping;
        if (ext4_map_extent(inode, logical, &mapping) != 0)
            break;
        if (ext4_read_block(mapping.start, block) != 0) {
            result = -1;
            break;
        }
        u32 offset = 0;
        struct ext4_dirent_info info;
        while (ext4_dir_next(block, &offset, &info) > 0) {
            u64 child_size = 0;
            int is_dir = (info.type == EXT4_FT_DIR);
            if (ext4_read_inode_raw(info.inode, child) == 0) {
                child_size = le32(child + EXT4_INODE_SIZE_OFF);
                is_dir = (le16(child) & EXT4_S_IFDIR) != 0;
            }
            callback(info.name, child_size, is_dir, context);
        }
    }

    kfree(block);
    kfree(child);
    return result;
}

int ext4_read_all(const char *path, u8 **buffer, u64 *size)
{
    if (!g_ext4.mounted || !buffer || !size)
        return -1;
    u32 ino = 0;
    if (walk_path(path, &ino, NULL) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    if ((le16(inode) & EXT4_S_IFDIR) != 0)
        return -1;

    u64 file_size = le32(inode + EXT4_INODE_SIZE_OFF);
    if (g_ext4.feature_ro_compat & 0x2)
        file_size |= (u64)le32(inode + 0x6C) << 32;
    if (file_size > EXT4_MAX_FILE_SIZE)
        return -1;

    u8 *data = file_size ? kmalloc(file_size) : NULL;
    if (file_size && !data)
        return -1;
    u64 read = 0;
    if (file_size &&
        ext4_read_inode_data(inode, 0, data, file_size, &read) != 0) {
        kfree(data);
        return -1;
    }
    *buffer = data;
    *size = read;
    return 0;
}

int ext4_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out)
{
    if (!g_ext4.mounted || (!dst && cap))
        return -1;
    u32 ino = 0;
    if (walk_path(path, &ino, NULL) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    if ((le16(inode) & EXT4_S_IFDIR) != 0)
        return -1;

    u64 file_size = le32(inode + EXT4_INODE_SIZE_OFF);
    if (g_ext4.feature_ro_compat & 0x2)
        file_size |= (u64)le32(inode + 0x6C) << 32;
    if (cap > file_size)
        cap = file_size;
    u64 read = 0;
    if (cap && ext4_read_inode_data(inode, 0, dst, cap, &read) != 0)
        return -1;
    if (read_out)
        *read_out = read;
    return 0;
}

int ext4_stat(const char *path, int *is_dir, u64 *size)
{
    if (!g_ext4.mounted)
        return -1;
    u32 ino = 0;
    if (walk_path(path, &ino, NULL) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    if (is_dir)
        *is_dir = (le16(inode) & EXT4_S_IFDIR) != 0;
    if (size) {
        u64 file_size = le32(inode + EXT4_INODE_SIZE_OFF);
        if (g_ext4.feature_ro_compat & 0x2)
            file_size |= (u64)le32(inode + 0x6C) << 32;
        *size = file_size;
    }
    return 0;
}

int ext4_file_extent(const char *path, u32 *start_block, u32 *blocks)
{
    if (!g_ext4.mounted)
        return -1;
    u32 ino = 0;
    if (walk_path(path, &ino, NULL) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;

    struct ext4_extent_path first;
    if (ext4_map_extent(inode, 0, &first) != 0)
        return -1;
    u32 total = (u32)((le32(inode + EXT4_INODE_SIZE_OFF) +
                       g_ext4.block_size - 1) /
                      g_ext4.block_size);
    if (first.count < total)
        return -1;
    u32 first_block = (u32)first.start;
    if (start_block)
        *start_block = first_block;
    if (blocks)
        *blocks = total;
    return 0;
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
    if (block_disk_read(disk, base + 2, EXT4_SB_SECTORS, sb) != 0)
        return -1;
    if (le16(sb + 0x38) != EXT4_MAGIC)
        return -1;
    u32 log = le32(sb + 0x18);
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
    g_ext4.block_size = 1024u << le32(sb + 0x18);
    g_ext4.sectors_per_block = g_ext4.block_size / 512;
    g_ext4.inodes_count = le32(sb + 0x00);
    g_ext4.blocks_count = le32(sb + 0x04);
    g_ext4.free_blocks = le32(sb + 0x0C);
    g_ext4.free_inodes = le32(sb + 0x10);
    g_ext4.first_data_block = le32(sb + 0x14);
    g_ext4.blocks_per_group = le32(sb + 0x20);
    g_ext4.inodes_per_group = le32(sb + 0x28);
    g_ext4.feature_incompat = le32(sb + 0x60);
    g_ext4.feature_ro_compat = le32(sb + 0x64);

    if (g_ext4.feature_incompat & EXT4_INCOMPAT_64BIT) {
        u32 blocks_hi = le32(sb + 0x150);
        u32 free_hi = le32(sb + 0x158);
        if (blocks_hi || free_hi)
            return -1;
        g_ext4.desc_size = le16(sb + 0xFE);
        if (g_ext4.desc_size < EXT4_GD_SIZE64)
            g_ext4.desc_size = EXT4_GD_SIZE64;
    } else {
        g_ext4.desc_size = EXT4_GD_SIZE32;
    }

    g_ext4.inode_size = le32(sb + 0x4C) >= 1 ? le16(sb + 0x58) : 128;
    if (g_ext4.inode_size < 128)
        g_ext4.inode_size = 128;
    g_ext4.first_ino = le32(sb + 0x4C) >= 1 ? le32(sb + 0x54) : 11;
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
    int disks = block_disk_count();
    int best_disk = -1;
    u64 best_base = 0;
    int best_score = 0;

    for (int disk = 0; disk < disks; disk++) {
        u64 sectors = block_disk_sectors(disk);
        if (sectors < 4)
            continue;

        consider(disk, 0, &best_disk, &best_base, &best_score);

        u8 sector[512];
        if (block_disk_read(disk, 0, 1, sector) != 0 ||
            sector[510] != 0x55 || sector[511] != 0xAA)
            continue;

        if (sector[446 + 4] == 0xEE) {
            u8 header[512];
            if (block_disk_read(disk, 1, 1, header) != 0 ||
                memcmp(header, "EFI PART", 8) != 0)
                continue;
            u64 entries_lba = le64(header + 72);
            u32 entries = le32(header + 80);
            u32 entry_size = le32(header + 84);
            if (entry_size < 128 || entries > 128 || entries_lba >= sectors)
                continue;
            for (u32 i = 0; i < entries; i++) {
                u64 offset = (u64)i * entry_size;
                u32 within = (u32)(offset % 512);
                if (within + 128 > 512)
                    continue;
                u8 entry[512];
                if (block_disk_read(disk, entries_lba + offset / 512, 1,
                                    entry) != 0)
                    continue;
                const u8 *item = entry + within;
                int used = 0;
                for (int k = 0; k < 16; k++)
                    used |= item[k];
                if (!used)
                    continue;
                u64 start = le64(item + 32);
                if (start == 0 || start >= sectors)
                    continue;
                consider(disk, start, &best_disk, &best_base, &best_score);
            }
        } else {
            for (int i = 0; i < 4; i++) {
                const u8 *entry = sector + 446 + i * 16;
                u8 type = entry[4];
                u64 start = le32(entry + 8);
                u32 count = le32(entry + 12);
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

    kprintf("[ext4] mounted \"%s\" on /mnt: disk %d, %u blocks, %u groups, "
            "%s\n",
            g_ext4.label, g_ext4.disk, g_ext4.blocks_count, g_ext4.groups,
            g_ext4.writable ? "rw" : "ro");
    return 0;
}
