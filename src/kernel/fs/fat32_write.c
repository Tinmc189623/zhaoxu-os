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

static void store_entry(u8 *entry, const char name[11], u32 first, u32 size);
static void mark_dirty(void);

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

/* One sector of the primary FAT is cached while a file's cluster chain is
 * being allocated.  Without this, writing a 400 KiB bootloader performed two
 * FAT sector writes per 512-byte cluster and took minutes in QEMU. */
static u32 g_fat_cache_sector = 0xFFFFFFFFu;
static u8 g_fat_cache[512];
static int g_fat_cache_dirty;

static int fat_cache_flush(void)
{
    struct fat32_fs *fs = fat32_state();
    if (!g_fat_cache_dirty || g_fat_cache_sector == 0xFFFFFFFFu)
        return 0;
    for (u32 copy = 0; copy < fs->num_fats; copy++) {
        u64 lba = fs->volume_base + (u64)copy * fs->fat_size +
                  g_fat_cache_sector;
        if (block_disk_write(fs->disk, lba, 1, g_fat_cache) != 0)
            return -1;
    }
    g_fat_cache_dirty = 0;
    return 0;
}

static int fat_cache_select(u32 sector)
{
    if (g_fat_cache_sector == sector)
        return 0;
    if (fat_cache_flush() != 0)
        return -1;
    if (read_sector(sector, g_fat_cache) != 0)
        return -1;
    g_fat_cache_sector = sector;
    return 0;
}

static u32 fat_read(u32 cluster)
{
    struct fat32_fs *fs = fat32_state();
    u32 offset = cluster * 4;
    u32 sector = fs->reserved_sectors + offset / 512;
    u32 index = (offset % 512) / 4;
    u8 buffer[512];
    const u8 *source = buffer;

    if (g_fat_cache_sector == sector) {
        source = g_fat_cache;
    } else if (read_sector(sector, buffer) != 0) {
        return 0xFFFFFFFFu;
    }
    u32 value = read_le32(source + index * 4) & 0x0FFFFFFFu;
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

    if (fat_cache_select(sector) != 0)
        return -1;
    u32 old = read_le32(g_fat_cache + index * 4);
    u32 merged = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
    u8 *slot = g_fat_cache + index * 4;
    slot[0] = (u8)merged;
    slot[1] = (u8)(merged >> 8);
    slot[2] = (u8)(merged >> 16);
    slot[3] = (u8)(merged >> 24);
    g_fat_cache_dirty = 1;
    fs->cache_valid = 0;
    return 0;
}

static u32 g_alloc_hint = 2;

void fat32_alloc_reset(void)
{
    g_alloc_hint = 2;
    g_fat_cache_sector = 0xFFFFFFFFu;
    g_fat_cache_dirty = 0;
}

static u32 fat_value(u32 cluster)
{
    struct fat32_fs *fs = fat32_state();
    u32 offset = cluster * 4;
    u32 sector = fs->reserved_sectors + offset / 512;
    if (g_fat_cache_sector == sector)
        return read_le32(g_fat_cache + (offset % 512)) & 0x0FFFFFFFu;
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
    struct fat32_fs *fs = fat32_state();
    u32 block = cluster_bytes();
    u32 cluster = first;
    u64 offset = 0;

    while (offset < size) {
        /* Collect a contiguous run of up to 8 clusters and write it with one
         * block request; a 750 KiB ESP payload then costs ~190 IPC calls
         * instead of one per 512-byte cluster. */
        u32 run = 1;
        u32 probe = cluster;
        while (run < 8 && offset + (u64)run * block < size) {
            u32 next = fat_read(probe);
            if (next != probe + 1)
                break;
            probe = next;
            run++;
        }
        u32 bytes = block * run;
        if (bytes > size - offset)
            bytes = (u32)(size - offset);
        u8 *buffer = kmalloc(block * run);
        if (!buffer)
            return -1;
        memset(buffer, 0, block * run);
        memcpy(buffer, data + offset, bytes);
        int result = block_disk_write(fs->disk, fs->volume_base +
                                      cluster_lba(cluster),
                                      fs->sectors_per_cluster * run,
                                      buffer);
        kfree(buffer);
        if (result != 0)
            return -1;

        offset += bytes;
        if (offset < size) {
            cluster = fat_read(probe);
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

/* Mark the FSInfo counters unknown instead of rescanning the whole FAT.
 * Scanning wrote one block read per FAT sector after every directory or file
 * operation; on a 64 MiB ESP that turned an install into a minute of I/O. */
static void mark_dirty(void)
{
    struct fat32_fs *fs = fat32_state();
    if (!fs->fsinfo_sector)
        return;
    u8 buffer[512];
    if (read_sector(fs->fsinfo_sector, buffer) != 0)
        return;
    for (int i = 0; i < 8; i++)
        buffer[0x1E8 + i] = 0xFF;
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

/* Build an 8.3 alias for a long name (LIMINE~1.SYS style). */
static int short_alias(const char *leaf, char output[11])
{
    memset(output, ' ', 11);
    int index = 0;
    const char *cursor = leaf;
    for (; *cursor && *cursor != '.' && index < 6; cursor++) {
        char character = *cursor;
        if (character >= 'a' && character <= 'z')
            character = (char)(character - 'a' + 'A');
        if (character == ' ' || character == ':' || character == '*' ||
            character == '?' || character == '"' || character == '<' ||
            character == '>' || character == '|' || character == '\\' ||
            character == '/')
            return -1;
        output[index++] = character;
    }
    if (index == 0)
        return -1;
    output[6] = '~';
    output[7] = '1';
    if (*cursor == '.') {
        cursor++;
        int extension = 0;
        for (; *cursor && extension < 3; cursor++) {
            char character = *cursor;
            if (character >= 'a' && character <= 'z')
                character = (char)(character - 'a' + 'A');
            if (character == ' ' || character == ':' || character == '*' ||
                character == '?' || character == '"' || character == '<' ||
                character == '>' || character == '|' || character == '\\' ||
                character == '/' || character == '.')
                return -1;
            output[8 + extension++] = character;
        }
    }
    return 0;
}

/* VFAT checksum over the 11-byte short name. */
static u8 vfat_checksum(const char name[11])
{
    u8 sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (u8)(((sum & 1) << 7) + (sum >> 1) + (u8)name[i]);
    return sum;
}

static void lfn_char(u8 *entry, int index, u16 value)
{
    int offset;
    if (index < 5)
        offset = 1 + index * 2;
    else if (index < 11)
        offset = 14 + (index - 5) * 2;
    else
        offset = 28 + (index - 11) * 2;
    entry[offset] = (u8)value;
    entry[offset + 1] = (u8)(value >> 8);
}

/* Append a VFAT long-name group plus its 8.3 entry to a directory.
 *
 * The walk uses fat_value (the write-side FAT cache), not fat_next_cluster:
 * that helper reports end-of-chain as 0xFFFFFFFF and re-reads a sector on
 * every call, which turned a full directory scan into minutes of failed
 * out-of-range block reads.  A fresh cluster is only chained when the last
 * cluster of the directory has no free run for the whole LFN group. */
static int append_long_entry(u32 directory, const char *leaf,
                             const char shortname[11], u32 first, u32 size)
{
    struct fat32_fs *fs = fat32_state();
    int length = 0;
    while (leaf[length])
        length++;
    if (length == 0 || length > 255)
        return -1;
    int entries = (length + 12) / 13;
    u8 checksum = vfat_checksum(shortname);
    u32 block = cluster_bytes();

    /* Find the last cluster that already belongs to the directory. */
    u32 tail = directory;
    u32 guard = fs->cluster_count + 2;
    for (;;) {
        if (guard-- == 0)
            return -1;
        u32 next = fat_value(tail);
        if (next < 2 || next >= FAT_END)
            break;
        tail = next;
    }

    u8 *buffer = kmalloc(block);
    if (!buffer)
        return -1;
    if (read_cluster(tail, buffer) != 0) {
        kfree(buffer);
        return -1;
    }

    /* A free slot is either an erased entry (0xE5) or past the end marker
     * (0x00).  The whole group must fit in one cluster. */
    int slots = entries + 1;
    long offset = -1;
    for (u32 at = 0; (u64)at + (u64)slots * 32 <= block; at += 32) {
        int free_run = 1;
        for (int k = 0; k < slots; k++) {
            u8 marker = buffer[at + (u32)k * 32];
            if (marker != 0x00 && marker != 0xE5) {
                free_run = 0;
                break;
            }
        }
        if (free_run) {
            offset = (long)at;
            break;
        }
    }

    u32 cluster = tail;
    if (offset < 0) {
        cluster = alloc_cluster();
        if (!cluster) {
            kfree(buffer);
            return -1;
        }
        if (fat_write(tail, cluster) != 0) {
            free_chain(cluster);
            kfree(buffer);
            return -1;
        }
        memset(buffer, 0, block);
        offset = 0;
    }

    u8 *group = buffer + offset;
    for (int sequence = entries; sequence >= 1; sequence--) {
        int slot = entries - sequence;
        u8 *entry = group + slot * 32;
        entry[0] = (u8)(sequence | (sequence == entries ? 0x40 : 0));
        entry[11] = 0x0F;
        entry[12] = 0;
        entry[13] = checksum;
        for (int i = 0; i < 13; i++) {
            int source = (sequence - 1) * 13 + i;
            u16 value = 0xFFFF;
            if (source < length)
                value = (u16)(u8)leaf[source];
            else if (source == length)
                value = 0x0000;
            lfn_char(entry, i, value);
        }
    }
    store_entry(group + entries * 32, shortname, first, size);

    int result = write_cluster(cluster, buffer);
    kfree(buffer);
    if (result != 0 || fat_cache_flush() != 0)
        return -1;
    return block_disk_flush(fs->disk) == 0 ? 0 : -1;
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
    int long_name = short_name_from(leaf, suffix) != 0;
    if (long_name && short_alias(leaf, suffix) != 0)
        return -1;

    struct dir_slot slot;
    memset(&slot, 0, sizeof(slot));
    if (!long_name) {
        if (locate_slot(parent, leaf, &slot) != 0)
            return -1;
        if (slot.found && slot.entry.is_dir)
            return -1;
    }

    u32 old_first = (!long_name && slot.found) ? slot.entry.cluster : 0;

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

    if (long_name) {
        if (append_long_entry(parent, leaf, suffix, first, (u32)size) != 0) {
            if (first)
                free_chain(first);
            return -1;
        }
        return 0;
    }

    if (commit_slot(&slot, suffix, first, (u32)size) != 0) {
        if (first)
            free_chain(first);
        return -1;
    }

    if (fat_cache_flush() != 0)
        return -1;
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
