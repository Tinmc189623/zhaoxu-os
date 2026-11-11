/*
 * ext4 core - directory entry iteration, path walking and listing.
 */
#include "ext4_core.h"

#include <string.h>
#include <types.h>

int ext4_dir_next(const u8 *block, u32 *offset,
                  struct ext4_dirent_info *info)
{
    u32 position = *offset;
    while (position + 8 <= g_ext4.block_size) {
        const u8 *entry = block + position;
        u32 inode = ext4_le32(entry);
        u16 rec_len = ext4_le16(entry + 4);
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
    u32 size = ext4_le32(inode + EXT4_INODE_SIZE_OFF);
    if (size == 0 || size > g_ext4.block_size * 1024u)
        return -1;

    u8 *block = ext4_alloc(g_ext4.block_size);
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

    ext4_free(block);
    return result;
}

int walk_path(const char *path, u32 *ino, u8 *type)
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
        if ((ext4_le16(inode) & EXT4_S_IFDIR) == 0)
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

int ext4_list(const char *path, ext4_list_cb callback, void *context)
{
    if (!g_ext4.mounted || !callback)
        return -1;

    u32 ino = 0;
    if (walk_path(path, &ino, NULL) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    if ((ext4_le16(inode) & EXT4_S_IFDIR) == 0)
        return -1;

    u32 size = ext4_le32(inode + EXT4_INODE_SIZE_OFF);
    u8 *block = ext4_alloc(g_ext4.block_size);
    u8 *child = ext4_alloc(256);
    if (!block || !child) {
        ext4_free(block);
        ext4_free(child);
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
                child_size = ext4_le32(child + EXT4_INODE_SIZE_OFF);
                is_dir = (ext4_le16(child) & EXT4_S_IFDIR) != 0;
            }
            callback(info.name, child_size, is_dir, context);
        }
    }

    ext4_free(block);
    ext4_free(child);
    return result;
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
        *is_dir = (ext4_le16(inode) & EXT4_S_IFDIR) != 0;
    if (size) {
        u64 file_size = ext4_le32(inode + EXT4_INODE_SIZE_OFF);
        if (g_ext4.feature_ro_compat & 0x2)
            file_size |= (u64)ext4_le32(inode + 0x6C) << 32;
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
    u32 total = (u32)((ext4_le32(inode + EXT4_INODE_SIZE_OFF) +
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
