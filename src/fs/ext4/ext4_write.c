/*
 * ext4 core - directory entry creation and file write paths.
 */
#include "ext4_core.h"

#include <string.h>
#include <types.h>

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
    u32 needed = 8 + ext4_align4(name_len);

    u8 inode[256];
    if (ext4_read_inode_raw(parent, inode) != 0 || !inode_is_dir(inode))
        return -1;

    u32 size = inode_size(inode);
    u8 *block = ext4_alloc(g_ext4.block_size);
    if (!block)
        return -1;

    for (u32 logical = 0; (u64)logical * g_ext4.block_size < size;
         logical++) {
        if (read_dir_block(inode, logical, block) != 0)
            break;
        u32 offset = 0;
        while (offset + 8 <= g_ext4.block_size) {
            u8 *entry = block + offset;
            u32 rec = ext4_le16(entry + 4);
            if (rec < 8 || offset + rec > g_ext4.block_size)
                break;
            u32 used = 0;
            if (ext4_le32(entry) != 0) {
                u32 existing = entry[6];
                used = 8 + ext4_align4(existing);
                if (existing == name_len &&
                    memcmp(entry + 8, name, name_len) == 0) {
                    ext4_free(block);
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
                    ext4_free(block);
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
                ext4_free(block);
                return result;
            }
            offset += rec;
        }
    }

    /* No room: append a fresh directory block. */
    u32 fresh_block = 0;
    if (ext4_alloc_run(1, &fresh_block) != 0) {
        ext4_free(block);
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
        ext4_free(block);
        ext4_free_run(fresh_block, 1);
        return -1;
    }

    u32 logical = size / g_ext4.block_size;
    if (inode_extent_append(inode, logical, fresh_block, 1) != 0) {
        ext4_free(block);
        ext4_free_run(fresh_block, 1);
        return -1;
    }
    inode_set_size(inode, size + g_ext4.block_size);
    ext4_put32(inode + EXT4_INODE_BLOCKS_OFF,
               ext4_le32(inode + EXT4_INODE_BLOCKS_OFF) + 8);
    int result = ext4_write_inode_raw(parent, inode);
    ext4_free(block);
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
    u8 *block = ext4_alloc(g_ext4.block_size);
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
            u32 rec = ext4_le16(entry + 4);
            if (rec < 8 || offset + rec > g_ext4.block_size)
                break;
            u32 existing = ext4_le32(entry) != 0 ? entry[6] : 0;
            if (existing == name_len &&
                memcmp(entry + 8, name, name_len) == 0) {
                if (previous != 0xFFFFFFFFu) {
                    u8 *prev = block + previous;
                    ext4_put16(prev + 4,
                               (u16)(ext4_le16(prev + 4) + rec));
                } else {
                    ext4_put32(entry, 0);
                }
                int result = write_dir_block(inode, logical, block);
                ext4_free(block);
                return result;
            }
            previous = offset;
            offset += rec;
        }
    }

    ext4_free(block);
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
        u8 *data = ext4_alloc(g_ext4.block_size);
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
        ext4_free(data);
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
        ext4_put32(inode + EXT4_INODE_BLOCKS_OFF, 8);

        u8 parent_inode[256];
        if (ext4_read_inode_raw(parent, parent_inode) == 0) {
            ext4_put16(parent_inode + 0x1A,
                       (u16)(ext4_le16(parent_inode + 0x1A) + 1));
            ext4_write_inode_raw(parent, parent_inode);
        }
        u8 desc[64];
        u32 group = (ino - 1) / g_ext4.inodes_per_group;
        if (ext4_read_group_desc(group, desc) == 0) {
            ext4_gd_set_used_dirs(desc,
                                  (u16)(ext4_gd_used_dirs(desc) + 1));
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
        ext4_logf("[ext4] split failed\n");
        return -1;
    }
    u32 parent = 0;
    if (ext4_lookup(parent_path, &parent) != 0) {
        ext4_logf("[ext4] parent %s lookup failed\n", parent_path);
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
            ext4_logf("[ext4] alloc inode failed\n");
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
            ext4_logf("[ext4] alloc run %u failed\n", blocks);
            if (fresh)
                ext4_free_inode(ino);
            return -1;
        }
        u8 *buffer = ext4_alloc(g_ext4.block_size);
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
                ext4_logf("[ext4] data block %u write failed\n", first + i);
                ext4_free(buffer);
                ext4_free_run(first, blocks);
                if (fresh)
                    ext4_free_inode(ino);
                return -1;
            }
        }
        ext4_free(buffer);
        if (inode_extent_append(inode, 0, first, blocks) != 0) {
            ext4_logf("[ext4] extent append failed\n");
            ext4_free_run(first, blocks);
            if (fresh)
                ext4_free_inode(ino);
            return -1;
        }
        ext4_put32(inode + EXT4_INODE_BLOCKS_OFF, blocks * 8u);
    } else {
        inode_clear_extents(inode);
    }
    inode_set_size(inode, (u32)size);

    if (ext4_write_inode_raw(ino, inode) != 0) {
        ext4_logf("[ext4] inode write failed\n");
        if (blocks)
            ext4_free_run(first, blocks);
        if (fresh)
            ext4_free_inode(ino);
        return -1;
    }
    if (fresh && ext4_dir_add(parent, name, ino, EXT4_FT_REG_FILE) != 0) {
        ext4_logf("[ext4] dir add failed\n");
        ext4_free_inode(ino);
        return -1;
    }
    return ext4_sync();
}

int ext4_write_file_contig(const char *path, const u8 *data, u64 size,
                           u32 *first, u32 *blocks)
{
    char parent_path[256];
    char name[256];
    if (split_parent(path, parent_path, name) != 0)
        return -1;
    u32 parent_ino = 0;
    if (ext4_lookup(parent_path, &parent_ino) != 0)
        return -1;

    u32 count = (u32)((size + g_ext4.block_size - 1) / g_ext4.block_size);
    u32 start = 0;
    if (ext4_alloc_run(count, &start) != 0)
        return -1;

    u32 ino = 0;
    if (ext4_create_node(parent_ino, name, 0100644, EXT4_FT_REG_FILE,
                         &ino) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    inode_init_extents(inode);
    if (inode_extent_append(inode, 0, start, count) != 0)
        return -1;
    inode_set_size(inode, (u32)size);
    ext4_put32(inode + EXT4_INODE_BLOCKS_OFF, count * 8);

    u8 *block = ext4_alloc(g_ext4.block_size);
    if (!block)
        return -1;
    for (u32 i = 0; i < count; i++) {
        u32 bytes = g_ext4.block_size;
        if ((u64)(i + 1) * g_ext4.block_size > size)
            bytes = (u32)(size - (u64)i * g_ext4.block_size);
        memset(block, 0, g_ext4.block_size);
        memcpy(block, data + (u64)i * g_ext4.block_size, bytes);
        if (ext4_write_block(start + i, block) != 0) {
            ext4_free(block);
            return -1;
        }
    }
    ext4_free(block);
    if (ext4_write_inode_raw(ino, inode) != 0)
        return -1;
    if (first)
        *first = start;
    if (blocks)
        *blocks = count;
    return ext4_sync();
}

/* e2fsck reads i_dtime as the orphan list's "next inode" pointer and reports
 * any deleted inode whose i_dtime is below s_inodes_count as part of a
 * corrupted orphan list (PR_1_LOW_DTIME).  The core has no clock, so stamp
 * deletion with a fixed epoch: above any inode number and safely in the
 * past. */
#define EXT4_DELETED_DTIME 946684800u

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

    /* Rewrite the inode as a clean deleted inode before dropping its bitmap
     * bit: e2fsck treats links=0/dtime!=0 with a live i_mode or extent root
     * as an orphan and refuses the filesystem.  i_block is cleared whole so
     * no extent pointers survive. */
    memset(inode + EXT4_INODE_BLOCK_OFF, 0, 60);
    ext4_put32(inode + EXT4_INODE_FLAGS_OFF, 0);
    ext4_put16(inode, 0);
    ext4_put16(inode + 0x1A, 0);
    inode_set_size(inode, 0);
    ext4_put32(inode + EXT4_INODE_BLOCKS_OFF, 0);
    ext4_put32(inode + 0x14, EXT4_DELETED_DTIME);
    if (ext4_write_inode_raw(ino, inode) != 0)
        return -1;
    if (ext4_dir_remove(parent, name) != 0)
        return -1;
    ext4_free_inode(ino);
    return ext4_sync();
}
