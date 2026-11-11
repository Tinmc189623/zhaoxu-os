/*
 * ext4 core - file read paths.
 */
#include "ext4_core.h"

#include <string.h>
#include <types.h>

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
    if ((ext4_le16(inode) & EXT4_S_IFDIR) != 0)
        return -1;

    u64 file_size = ext4_le32(inode + EXT4_INODE_SIZE_OFF);
    if (g_ext4.feature_ro_compat & 0x2)
        file_size |= (u64)ext4_le32(inode + 0x6C) << 32;
    if (file_size > EXT4_MAX_FILE_SIZE)
        return -1;

    u8 *data = file_size ? ext4_alloc((u32)file_size) : NULL;
    if (file_size && !data)
        return -1;
    u64 read = 0;
    if (file_size &&
        ext4_read_inode_data(inode, 0, data, file_size, &read) != 0) {
        ext4_free(data);
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
    if ((ext4_le16(inode) & EXT4_S_IFDIR) != 0)
        return -1;

    u64 file_size = ext4_le32(inode + EXT4_INODE_SIZE_OFF);
    if (g_ext4.feature_ro_compat & 0x2)
        file_size |= (u64)ext4_le32(inode + 0x6C) << 32;
    if (cap > file_size)
        cap = file_size;
    u64 read = 0;
    if (cap && ext4_read_inode_data(inode, 0, dst, cap, &read) != 0)
        return -1;
    if (read_out)
        *read_out = read;
    return 0;
}

int ext4_read_at(const char *path, u64 offset, u8 *dst, u64 cap,
                 u64 *read_out)
{
    u32 ino = 0;
    if (ext4_lookup(path, &ino) != 0)
        return -1;
    u8 inode[256];
    if (ext4_read_inode_raw(ino, inode) != 0)
        return -1;
    if ((ext4_le16(inode) & EXT4_S_IFDIR) != 0)
        return -1;
    u64 read = 0;
    if (ext4_read_inode_data(inode, offset, dst, cap, &read) != 0)
        return -1;
    if (read_out)
        *read_out = read;
    return 0;
}
