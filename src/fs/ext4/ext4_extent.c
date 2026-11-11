/*
 * ext4 core - extent tree lookup and inode data mapping.
 */
#include "ext4_core.h"

#include <string.h>
#include <types.h>

static int extent_lookup(const u8 *node, u16 depth, u32 logical,
                         struct ext4_extent_path *path)
{
    if (ext4_le16(node) != EXT4_EXTENT_MAGIC)
        return -1;
    u16 entries = ext4_le16(node + 2);
    if (entries > 1024)
        return -1;

    if (depth == 0) {
        for (u16 i = 0; i < entries; i++) {
            const u8 *entry = node + 12 + (u32)i * 12;
            u32 start = ext4_le32(entry);
            u16 length = ext4_le16(entry + 4);
            if (length > 0x8000)
                length = (u16)(length - 0x8000);
            if (length == 0)
                continue;
            if (logical < start || logical >= start + length)
                continue;
            u64 physical = ((u64)ext4_le16(entry + 6) << 32) |
                           ext4_le32(entry + 8);
            path->start = physical + (logical - start);
            path->count = start + length - logical;
            return 0;
        }
        return -1;
    }

    for (u16 i = 0; i < entries; i++) {
        const u8 *entry = node + 12 + (u32)i * 12;
        u32 start = ext4_le32(entry);
        u32 next = i + 1 < entries ? ext4_le32(entry + 12) : 0xFFFFFFFFu;
        if (logical < start || logical >= next)
            continue;
        u64 leaf = ((u64)ext4_le16(entry + 8) << 32) | ext4_le32(entry + 4);
        u8 *buffer = ext4_alloc(g_ext4.block_size);
        if (!buffer)
            return -1;
        int result = ext4_read_block(leaf, buffer);
        if (result == 0)
            result = extent_lookup(buffer, (u16)(depth - 1), logical, path);
        ext4_free(buffer);
        return result;
    }
    return -1;
}

int ext4_map_extent(const u8 *inode, u32 logical,
                    struct ext4_extent_path *path)
{
    if (!path ||
        !(ext4_le32(inode + EXT4_INODE_FLAGS_OFF) & EXT4_EXTENTS_FL))
        return -1;
    const u8 *node = inode + EXT4_INODE_BLOCK_OFF;
    if (ext4_le16(node) != EXT4_EXTENT_MAGIC)
        return -1;
    return extent_lookup(node, ext4_le16(node + 6), logical, path);
}

int ext4_read_inode_data(const u8 *inode, u64 offset, void *buffer,
                         u64 length, u64 *read_out)
{
    u64 size = ext4_le32(inode + EXT4_INODE_SIZE_OFF);
    if (g_ext4.feature_ro_compat & 0x2)
        size |= (u64)ext4_le32(inode + 0x6C) << 32;
    if (offset >= size) {
        if (read_out)
            *read_out = 0;
        return 0;
    }
    if (offset + length > size)
        length = size - offset;

    u8 *block = ext4_alloc(g_ext4.block_size);
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

    ext4_free(block);
    if (read_out)
        *read_out = done;
    return result;
}
