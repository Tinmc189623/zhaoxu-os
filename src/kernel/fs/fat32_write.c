/*
 * Zhaoxu OS - FAT32 write path.
 *
 * Creates, overwrites and deletes 8.3 files inside an existing directory.
 * Long-name creation, directory creation and FSInfo free counters are out
 * of scope; the FSInfo counters are reset to "unknown" after every change.
 */
#include "fat32_internal.h"

#include <block.h>
#include <mem.h>
#include <string.h>
#include <types.h>

#define FAT_EOC 0x0FFFFFFFu
#define FAT_END 0x0FFFFFF8u

struct dir_slot {
    u32 cluster;
    u32 offset;
    int found;
    struct fat_entry entry;
};

static u32 cluster_bytes(void)
{
    return fat32_state()->sectors_per_cluster * 512;
}

static u64 cluster_lba(u32 cluster)
{
    struct fat32_fs *fs = fat32_state();
    return (u64)fs->data_start +
           (u64)(cluster - 2) * fs->sectors_per_cluster;
}

static int write_cluster(u32 cluster, const u8 *buffer)
{
    struct fat32_fs *fs = fat32_state();
    return block_disk_write(fs->disk, fs->volume_base + cluster_lba(cluster),
                           fs->sectors_per_cluster, buffer);
}

static u32 fat_read(u32 cluster)
{
    struct fat32_fs *fs = fat32_state();
    u32 offset = cluster * 4;
    u32 sector = fs->reserved_sectors + offset / 512;
    u32 index = (offset % 512) / 4;
    u8 buffer[512];

    if (read_sector(sector, buffer) != 0)
        return 0xFFFFFFFFu;
    u32 value = read_le32(buffer + index * 4) & 0x0FFFFFFFu;
    if (value == 0 || value >= FAT_END)
        return 0xFFFFFFFFu;
    return value;
}

static int fat_write(u32 cluster, u32 value)
{
    struct fat32_fs *fs = fat32_state();
    u32 offset = cluster * 4;
    u32 sector = fs->reserved_sectors + offset / 512;
    u32 index = (offset % 512) / 4;
    u8 buffer[512];

    for (u32 copy = 0; copy < fs->num_fats; copy++) {
        u64 lba = fs->volume_base + (u64)copy * fs->fat_size + sector;
        if (block_disk_read(fs->disk, lba, 1, buffer) != 0)
            return -1;
        u32 old = read_le32(buffer + index * 4);
        u32 merged = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
        u8 *slot = buffer + index * 4;
        slot[0] = (u8)merged;
        slot[1] = (u8)(merged >> 8);
        slot[2] = (u8)(merged >> 16);
        slot[3] = (u8)(merged >> 24);
        if (block_disk_write(fs->disk, lba, 1, buffer) != 0)
            return -1;
    }
    fs->cache_valid = 0;
    return 0;
}

static u32 g_alloc_hint = 2;

void fat32_alloc_reset(void)
{
    g_alloc_hint = 2;
}

static u32 fat_value(u32 cluster)
{
    struct fat32_fs *fs = fat32_state();
    u32 offset = cluster * 4;
    u32 sector = fs->reserved_sectors + offset / 512;
    u8 buffer[512];
    if (read_sector(sector, buffer) != 0)
        return 0xFFFFFFFFu;
    return read_le32(buffer + (offset % 512)) & 0x0FFFFFFFu;
}

static u32 alloc_cluster(void)
{
    struct fat32_fs *fs = fat32_state();
    u32 total = fs->cluster_count + 2;
    u32 start = g_alloc_hint;
    if (start < 2 || start >= total)
        start = 2;

    for (u32 pass = 0; pass < 2; pass++) {
        u32 from = pass ? 2 : start;
        u32 to = pass ? start : total;
        for (u32 cluster = from; cluster < to; cluster++) {
            if (fat_value(cluster) != 0)
                continue;
            if (fat_write(cluster, FAT_EOC) != 0)
                return 0;
            g_alloc_hint = cluster + 1;
            return cluster;
        }
    }
    return 0;
}

static int free_chain(u32 first)
{
    struct fat32_fs *fs = fat32_state();
    u32 guard = fs->cluster_count + 2;

    u32 cluster = first;
    while (cluster >= 2 && cluster < FAT_END && guard-- > 0) {
        u32 next = fat_read(cluster);
        if (fat_write(cluster, 0) != 0)
            return -1;
        cluster = next;
    }
    return 0;
}

static int write_chain(u32 first, const u8 *data, u64 size)
{
    u32 block = cluster_bytes();
    u32 cluster = first;
    u64 offset = 0;

    while (offset < size) {
        u32 chunk = (size - offset) > block ? block : (u32)(size - offset);
        u8 *buffer = kmalloc(block);
        if (!buffer)
            return -1;
        memset(buffer, 0, block);
        memcpy(buffer, data + offset, chunk);
        int result = write_cluster(cluster, buffer);
        kfree(buffer);
        if (result != 0)
            return -1;

        offset += block;
        if (offset < size) {
            cluster = fat_read(cluster);
            if (cluster < 2)
                return -1;
        }
    }
    return 0;
}

static int short_name_from(const char *leaf, char output[11])
{
    memset(output, ' ', 11);

    const char *cursor = leaf;
    int index = 0;
    for (; *cursor && *cursor != '.'; cursor++) {
        char character = *cursor;
        if (character >= 'a' && character <= 'z')
            character = (char)(character - 'a' + 'A');
        if (character == ' ' || character == ':' || character == '*' ||
            character == '?' || character == '"' || character == '<' ||
            character == '>' || character == '|' || character == '\\' ||
            character == '/')
            return -1;
        if (index >= 8)
            return -1;
        output[index++] = character;
    }
    if (*cursor == '.') {
        cursor++;
        int extension = 0;
        for (; *cursor; cursor++) {
            char character = *cursor;
            if (character >= 'a' && character <= 'z')
                character = (char)(character - 'a' + 'A');
            if (character == ' ' || character == ':' || character == '*' ||
                character == '?' || character == '"' || character == '<' ||
                character == '>' || character == '|' || character == '\\' ||
                character == '/' || character == '.')
                return -1;
            if (extension >= 3)
                return -1;
            output[8 + extension++] = character;
        }
    }
    return index > 0 ? 0 : -1;
}

static int resolve_parent(const char *path, u32 *parent, char *leaf)
{
    char copy[256];
    usize length = 0;
    while (path[length]) {
        if (length + 1 >= sizeof(copy))
            return -1;
        copy[length] = path[length];
        length++;
    }
    copy[length] = 0;

    if (copy[0] != '/')
        return -1;
    while (length > 1 && copy[length - 1] == '/')
        copy[--length] = 0;
    if (length <= 1)
        return -1;

    long slash = -1;
    for (long i = (long)length - 1; i >= 0; i--) {
        if (copy[i] == '/') {
            slash = i;
            break;
        }
    }
    if (slash < 0)
        return -1;

    usize leaf_length = length - (usize)slash - 1;
    if (leaf_length == 0 || leaf_length >= 256)
        return -1;
    memcpy(leaf, copy + slash + 1, leaf_length);
    leaf[leaf_length] = 0;

    if (slash == 0)
        copy[1] = 0;
    else
        copy[slash] = 0;

    struct fat_entry entry;
    if (fat32_find(copy, &entry) != 0 || !entry.is_dir)
        return -1;
    *parent = entry.cluster;
    return 0;
}

static int locate_slot(u32 directory, const char *leaf, struct dir_slot *slot)
{
    struct fat32_fs *fs = fat32_state();
    u32 block = cluster_bytes();
    u8 *buffer = kmalloc(block);
    if (!buffer)
        return -1;

    int free_found = 0;
    int end_seen = 0;
    u32 cluster = directory;
    u32 last = directory;
    u32 guard = fs->cluster_count + 2;

    memset(slot, 0, sizeof(*slot));
    while (cluster >= 2 && cluster < FAT_END && guard-- > 0) {
        if (read_cluster(cluster, buffer) != 0) {
            kfree(buffer);
            return -1;
        }
        for (u32 offset = 0; offset < block; offset += 32) {
            const u8 *entry = buffer + offset;
            if (entry[0] == 0x00) {
                if (!free_found) {
                    slot->cluster = cluster;
                    slot->offset = offset;
                    free_found = 1;
                }
                end_seen = 1;
                break;
            }
            if (entry[0] == 0xE5) {
                if (!free_found) {
                    slot->cluster = cluster;
                    slot->offset = offset;
                    free_found = 1;
                }
                continue;
            }
            u8 attributes = entry[11];
            if (attributes == 0x0F || (attributes & 0x08))
                continue;
            if (entry[0] == '.' && (entry[1] == ' ' || entry[1] == '.'))
                continue;

            char name[256];
            short_name(entry, name);
            if (!name_equal(name, leaf))
                continue;

            slot->cluster = cluster;
            slot->offset = offset;
            slot->found = 1;
            strcpy(slot->entry.name, name);
            slot->entry.cluster =
                ((u32)read_le16(entry + 20) << 16) | read_le16(entry + 26);
            slot->entry.size = read_le32(entry + 28);
            slot->entry.is_dir = (attributes & 0x10) != 0;
            kfree(buffer);
            return 0;
        }
        last = cluster;
        if (end_seen)
            break;
        cluster = fat_next_cluster(cluster);
    }

    if (free_found) {
        kfree(buffer);
        return 0;
    }

    u32 extra = alloc_cluster();
    if (!extra) {
        kfree(buffer);
        return -1;
    }
    u8 *zero = kmalloc(block);
    if (!zero) {
        free_chain(extra);
        kfree(buffer);
        return -1;
    }
    memset(zero, 0, block);
    int result = write_cluster(extra, zero);
    kfree(zero);
    if (result != 0 || fat_write(last, extra) != 0) {
        free_chain(extra);
        kfree(buffer);
        return -1;
    }

    slot->cluster = extra;
    slot->offset = 0;
    kfree(buffer);
    return 0;
}

static void store_entry(u8 *entry, const char name[11], u32 first, u32 size)
{
    memset(entry, 0, 32);
    memcpy(entry, name, 11);
    entry[11] = 0x20;
    entry[20] = (u8)(first >> 16);
    entry[21] = (u8)(first >> 24);
    entry[26] = (u8)first;
    entry[27] = (u8)(first >> 8);
    entry[28] = (u8)size;
    entry[29] = (u8)(size >> 8);
    entry[30] = (u8)(size >> 16);
    entry[31] = (u8)(size >> 24);
}

static void update_entry(u8 *entry, u32 first, u32 size)
{
    entry[20] = (u8)(first >> 16);
    entry[21] = (u8)(first >> 24);
    entry[26] = (u8)first;
    entry[27] = (u8)(first >> 8);
    entry[28] = (u8)size;
    entry[29] = (u8)(size >> 8);
    entry[30] = (u8)(size >> 16);
    entry[31] = (u8)(size >> 24);
    entry[11] |= 0x20;
}

static void mark_dirty(void)
{
    struct fat32_fs *fs = fat32_state();
    if (!fs->fsinfo_sector)
        return;

    u32 total = fs->cluster_count + 2;
    u32 free_count = 0;
    u32 next_free = 0xFFFFFFFFu;
    u8 buffer[512];

    for (u32 sector = 0; sector < fs->fat_size; sector++) {
        if (read_sector(fs->reserved_sectors + sector, buffer) != 0)
            return;
        for (u32 index = 0; index < 128; index++) {
            u32 cluster = sector * 128 + index;
            if (cluster < 2 || cluster >= total)
                continue;
            if ((read_le32(buffer + index * 4) & 0x0FFFFFFFu) != 0)
                continue;
            free_count++;
            if (next_free == 0xFFFFFFFFu)
                next_free = cluster;
        }
    }

    if (read_sector(fs->fsinfo_sector, buffer) != 0)
        return;
    u8 *free = buffer + 0x1E8;
    free[0] = (u8)free_count;
    free[1] = (u8)(free_count >> 8);
    free[2] = (u8)(free_count >> 16);
    free[3] = (u8)(free_count >> 24);
    free[4] = (u8)next_free;
    free[5] = (u8)(next_free >> 8);
    free[6] = (u8)(next_free >> 16);
    free[7] = (u8)(next_free >> 24);
    block_disk_write(fs->disk, fs->volume_base + fs->fsinfo_sector, 1, buffer);
}

static int commit_slot(const struct dir_slot *slot, const char suffix[11],
                       u32 first, u32 size)
{
    u8 *buffer = kmalloc(cluster_bytes());
    if (!buffer)
        return -1;
    if (read_cluster(slot->cluster, buffer) != 0) {
        kfree(buffer);
        return -1;
    }
    u8 *entry = buffer + slot->offset;
    if (slot->found)
        update_entry(entry, first, size);
    else
        store_entry(entry, suffix, first, size);

    int result = write_cluster(slot->cluster, buffer);
    kfree(buffer);
    return result;
}

int fat32_write_file(const char *path, const u8 *data, u64 size)
{
    struct fat32_fs *fs = fat32_state();
    if (!fs->mounted || size > FAT32_MAX_FILE_SIZE)
        return -1;

    char leaf[256];
    u32 parent = 0;
    if (resolve_parent(path, &parent, leaf) != 0)
        return -1;

    char suffix[11];
    if (short_name_from(leaf, suffix) != 0)
        return -1;

    struct dir_slot slot;
    if (locate_slot(parent, leaf, &slot) != 0)
        return -1;
    if (slot.found && slot.entry.is_dir)
        return -1;

    u32 old_first = slot.found ? slot.entry.cluster : 0;

    u32 first = 0;
    u32 previous = 0;
    if (size > 0) {
        u32 block = cluster_bytes();
        u32 needed = (u32)((size + block - 1) / block);
        for (u32 i = 0; i < needed; i++) {
            u32 cluster = alloc_cluster();
            if (!cluster) {
                if (first)
                    free_chain(first);
                return -1;
            }
            if (i == 0)
                first = cluster;
            else if (fat_write(previous, cluster) != 0) {
                free_chain(first);
                return -1;
            }
            previous = cluster;
        }
        if (fat_write(previous, FAT_EOC) != 0 ||
            write_chain(first, data, size) != 0) {
            free_chain(first);
            return -1;
        }
    }

    if (old_first >= 2)
        free_chain(old_first);

    if (commit_slot(&slot, suffix, first, (u32)size) != 0) {
        if (first)
            free_chain(first);
        return -1;
    }

    mark_dirty();
    return block_disk_flush(fs->disk) == 0 ? 0 : -1;
}

int fat32_unlink(const char *path)
{
    struct fat32_fs *fs = fat32_state();
    if (!fs->mounted)
        return -1;

    char leaf[256];
    u32 parent = 0;
    if (resolve_parent(path, &parent, leaf) != 0)
        return -1;

    struct dir_slot slot;
    if (locate_slot(parent, leaf, &slot) != 0 || !slot.found)
        return -1;
    if (slot.entry.is_dir)
        return -1;

    u8 *buffer = kmalloc(cluster_bytes());
    if (!buffer)
        return -1;
    if (read_cluster(slot.cluster, buffer) != 0) {
        kfree(buffer);
        return -1;
    }
    buffer[slot.offset] = 0xE5;
    int result = write_cluster(slot.cluster, buffer);
    kfree(buffer);
    if (result != 0)
        return -1;

    if (slot.entry.cluster >= 2)
        free_chain(slot.entry.cluster);
    mark_dirty();
    return block_disk_flush(fs->disk) == 0 ? 0 : -1;
}

static void store_dir_entry(u8 *entry, const char name[11], u32 first)
{
    memset(entry, 0, 32);
    memcpy(entry, name, 11);
    entry[11] = 0x10;
    entry[20] = (u8)(first >> 16);
    entry[21] = (u8)(first >> 24);
    entry[26] = (u8)first;
    entry[27] = (u8)(first >> 8);
}

int fat32_mkdir(const char *path)
{
    struct fat32_fs *fs = fat32_state();
    if (!fs->mounted)
        return -1;

    char leaf[256];
    u32 parent = 0;
    if (resolve_parent(path, &parent, leaf) != 0)
        return -1;

    char suffix[11];
    if (short_name_from(leaf, suffix) != 0)
        return -1;

    struct dir_slot slot;
    if (locate_slot(parent, leaf, &slot) != 0)
        return -1;
    if (slot.found)
        return slot.entry.is_dir ? 0 : -1;

    u32 cluster = alloc_cluster();
    if (!cluster)
        return -1;

    u8 *block = kmalloc(cluster_bytes());
    if (!block) {
        free_chain(cluster);
        return -1;
    }
    memset(block, 0, cluster_bytes());

    char dot[11];
    char dotdot[11];
    memset(dot, ' ', sizeof(dot));
    memset(dotdot, ' ', sizeof(dotdot));
    dot[0] = '.';
    dotdot[0] = '.';
    dotdot[1] = '.';
    store_dir_entry(block, dot, cluster);
    store_dir_entry(block + 32, dotdot, parent == fs->root_cluster ? 0 : parent);
    int result = write_cluster(cluster, block);
    kfree(block);
    if (result != 0) {
        free_chain(cluster);
        return -1;
    }

    u8 *buffer = kmalloc(cluster_bytes());
    if (!buffer) {
        free_chain(cluster);
        return -1;
    }
    if (read_cluster(slot.cluster, buffer) != 0) {
        kfree(buffer);
        free_chain(cluster);
        return -1;
    }
    store_dir_entry(buffer + slot.offset, suffix, cluster);
    result = write_cluster(slot.cluster, buffer);
    kfree(buffer);
    if (result != 0) {
        free_chain(cluster);
        return -1;
    }

    mark_dirty();
    return block_disk_flush(fs->disk) == 0 ? 0 : -1;
}
