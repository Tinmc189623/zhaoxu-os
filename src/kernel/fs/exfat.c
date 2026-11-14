/*
 * Zhaoxu OS - read-only exFAT driver.
 *
 * exFAT stores one file as a set of 32-byte entries: a file entry, a stream
 * extension and one or more name entries.  This driver walks those sets,
 * supports both FAT-chained and contiguous (NoFatChain) allocation, and
 * honours ValidDataLength by zero-filling the tail of a sparse file.
 */
#include <blockfs.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define EXFAT_NAME_MAX 256
#define EXFAT_DIR_MAX (4u * 1024u * 1024u)
#define EXFAT_CHAIN_MAX 65536u

struct exfat_node {
    char name[EXFAT_NAME_MAX];
    u32 cluster;
    u64 size;
    u64 valid_size;
    int is_dir;
    int nofat;
};

struct exfat {
    struct blockfs_volume vol;
    u32 sector_size;
    u32 cluster_size;
    u32 fat_offset;
    u32 heap_offset;
    u32 cluster_count;
    u32 root_cluster;
};

static u64 cluster_offset(const struct exfat *fs, u32 cluster)
{
    return (u64)fs->heap_offset +
           (u64)(cluster - 2) * fs->cluster_size;
}

static int exfat_next(struct exfat *fs, u32 cluster)
{
    u8 bytes[4];
    if (cluster < 2 || cluster >= fs->cluster_count + 2)
        return -1;
    if (bf_read_at(&fs->vol, fs->fat_offset + (u64)cluster * 4, bytes, 4))
        return -1;
    u32 value = bf_le32(bytes);
    if (value >= 0xFFFFFFF8u || value < 2 ||
        value >= fs->cluster_count + 2)
        return -1;
    return (int)value;
}

/* Load a directory.  Root directories always use the FAT; a subdirectory
 * uses its stream extension flags and known data length. */
static int exfat_load_dir(struct exfat *fs, u32 cluster, int nofat, u64 size,
                          u8 **buffer, u64 *out_size)
{
    if (cluster < 2 || cluster >= fs->cluster_count + 2)
        return -1;

    if (nofat) {
        if (!size)
            size = (u64)fs->cluster_size;
        if (size > EXFAT_DIR_MAX)
            size = EXFAT_DIR_MAX;
        u8 *data = kmalloc((usize)size);
        if (!data)
            return -1;
        if (bf_read_at(&fs->vol, cluster_offset(fs, cluster), data,
                       (u32)size) != 0) {
            kfree(data);
            return -1;
        }
        *buffer = data;
        *out_size = size;
        return 0;
    }

    u64 capacity = (u64)fs->cluster_size * 4;
    if (capacity > EXFAT_DIR_MAX)
        capacity = EXFAT_DIR_MAX;
    if (size && size < capacity)
        capacity = size;
    if (!capacity)
        capacity = fs->cluster_size;

    u8 *data = kmalloc((usize)capacity);
    if (!data)
        return -1;
    u64 used = 0;
    u32 current = cluster;
    for (u32 step = 0; step < EXFAT_CHAIN_MAX; step++) {
        if (used + fs->cluster_size > capacity)
            break;
        if (bf_read_at(&fs->vol, cluster_offset(fs, current), data + used,
                       fs->cluster_size) != 0)
            break;
        used += fs->cluster_size;
        if (size && used >= size)
            break;
        int next = exfat_next(fs, current);
        if (next < 0)
            break;
        current = (u32)next;
    }
    if (!used) {
        kfree(data);
        return -1;
    }
    *buffer = data;
    *out_size = used;
    return 0;
}

/* Decode a stream extension entry into a node. */
static void exfat_stream(struct exfat *fs, const u8 *entry,
                         struct exfat_node *node)
{
    u16 flags = bf_le16(entry + 1);
    node->nofat = (flags & 0x02) != 0;
    node->valid_size = bf_le64(entry + 8);
    node->cluster = bf_le32(entry + 20);
    node->size = bf_le64(entry + 24);
    (void)fs;
}

/* Parse one complete file entry set.  `entries` is the first entry and
 * `available` bounds the secondary entries. */
static int exfat_parse_set(struct exfat *fs, const u8 *entries,
                           u32 available, struct exfat_node *node)
{
    if (available < 2 || entries[0] != 0x85)
        return -1;
    u32 secondaries = entries[1];
    if (secondaries + 1u > available || secondaries < 1)
        return -1;

    memset(node, 0, sizeof(*node));
    u8 attributes = entries[4];
    node->is_dir = (attributes & 0x10) != 0;

    const u8 *stream = NULL;
    u16 name16[EXFAT_NAME_MAX];
    u32 name_units = 0;
    u32 name_index = 0;
    memset(name16, 0, sizeof(name16));

    for (u32 i = 1; i <= secondaries; i++) {
        const u8 *entry = entries + i * 32;
        if (entry[0] == 0xC0) {
            stream = entry;
        } else if (entry[0] == 0xC1) {
            /* Name entries start at character 0 regardless of their slot. */
            u32 base = name_index * 15;
            name_index++;
            for (u32 k = 0; k < 15 && base + k < EXFAT_NAME_MAX; k++) {
                u16 unit = bf_le16(entry + 2 + k * 2);
                if (!unit && name_units <= base + k)
                    break;
                name16[base + k] = unit;
                if (base + k + 1 > name_units)
                    name_units = base + k + 1;
            }
        }
    }
    if (!stream)
        return -1;
    exfat_stream(fs, stream, node);
    if (name_units > EXFAT_NAME_MAX - 1)
        name_units = EXFAT_NAME_MAX - 1;
    bf_utf16_to_utf8(name16, name_units, node->name, sizeof(node->name));
    return node->name[0] ? 0 : -1;
}

static int exfat_find(struct exfat *fs, const char *path,
                      struct exfat_node *result)
{
    if (!path || path[0] != '/')
        return -1;

    u32 cluster = fs->root_cluster;
    int nofat = 0;
    u64 dir_size = 0;
    if (!path[1]) {
        memset(result, 0, sizeof(*result));
        result->name[0] = '/';
        result->cluster = cluster;
        result->is_dir = 1;
        return 0;
    }

    const char *cursor = path + 1;
    while (*cursor) {
        char component[EXFAT_NAME_MAX];
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 < sizeof(component))
                component[length] = *cursor;
            length++;
            cursor++;
        }
        while (*cursor == '/')
            cursor++;
        if (!length || length >= sizeof(component))
            return -1;
        component[length] = 0;

        u8 *data = NULL;
        u64 size = 0;
        if (exfat_load_dir(fs, cluster, nofat, dir_size, &data, &size))
            return -1;

        struct exfat_node found;
        int matched = 0;
        u32 offset = 0;
        while (offset + 32 <= size) {
            u8 type = data[offset];
            if (type == 0x00)
                break;
            if (type == 0x85 && exfat_parse_set(
                    fs, data + offset, (u32)(size - offset) / 32,
                    &found) == 0) {
                if (bf_name_equal(found.name, component)) {
                    matched = 1;
                    break;
                }
                offset += (u32)(data[offset + 1] + 1) * 32;
                continue;
            }
            offset += 32;
        }
        kfree(data);
        if (!matched)
            return -1;
        if (!*cursor) {
            *result = found;
            return 0;
        }
        if (!found.is_dir)
            return -1;
        cluster = found.cluster;
        nofat = found.nofat;
        dir_size = found.size;
    }
    return -1;
}

static int exfat_list(void *private, const char *rel, vfs_list_cb callback,
                      void *context)
{
    struct exfat *fs = private;
    u32 cluster = fs->root_cluster;
    int nofat = 0;
    u64 dir_size = 0;

    if (rel && rel[0]) {
        char path[VFS_PATH_MAX];
        usize length = strlen(rel);
        if (length + 2 > sizeof(path))
            return -1;
        path[0] = '/';
        memcpy(path + 1, rel, length + 1);
        struct exfat_node node;
        if (exfat_find(fs, path, &node) != 0 || !node.is_dir)
            return -1;
        cluster = node.cluster;
        nofat = node.nofat;
        dir_size = node.size;
    }

    u8 *data = NULL;
    u64 size = 0;
    if (exfat_load_dir(fs, cluster, nofat, dir_size, &data, &size))
        return -1;

    u32 offset = 0;
    while (offset + 32 <= size) {
        u8 type = data[offset];
        if (type == 0x00)
            break;
        if (type == 0x85) {
            struct exfat_node node;
            if (exfat_parse_set(fs, data + offset,
                                (u32)(size - offset) / 32, &node) == 0)
                callback(node.name, node.size, node.is_dir, context);
            offset += (u32)(data[offset + 1] + 1) * 32;
            continue;
        }
        offset += 32;
    }
    kfree(data);
    return 0;
}

static int exfat_stat(void *private, const char *rel, int *is_dir, u64 *size)
{
    struct exfat *fs = private;
    char path[VFS_PATH_MAX];
    usize length = strlen(rel ? rel : "");
    if (length + 2 > sizeof(path))
        return -1;
    path[0] = '/';
    if (length)
        memcpy(path + 1, rel, length + 1);
    else
        path[1] = 0;

    struct exfat_node node;
    if (exfat_find(fs, path, &node) != 0)
        return -1;
    if (is_dir)
        *is_dir = node.is_dir;
    if (size)
        *size = node.size;
    return 0;
}

/* Copy from a contiguous allocation (NoFatChain). */
static int exfat_read_contiguous(struct exfat *fs, u32 start, u64 offset,
                                 u8 *dst, u64 length, u64 *read_out)
{
    u64 base = cluster_offset(fs, start) + offset;
    if (bf_read_at(&fs->vol, base, dst, (u32)length) != 0)
        return -1;
    if (read_out)
        *read_out = length;
    return 0;
}

/* Copy from a FAT-chained allocation. */
static int exfat_read_chain(struct exfat *fs, u32 start, u64 offset,
                            u8 *dst, u64 length, u64 *read_out)
{
    u32 cluster = start;
    u64 skip = offset;
    u64 done = 0;
    for (u32 step = 0; step < EXFAT_CHAIN_MAX && skip >= fs->cluster_size;
         step++) {
        int next = exfat_next(fs, cluster);
        if (next < 0)
            return -1;
        cluster = (u32)next;
        skip -= fs->cluster_size;
    }
    for (u32 step = 0; step < EXFAT_CHAIN_MAX && done < length; step++) {
        u64 base = cluster_offset(fs, cluster) + skip;
        u64 available = fs->cluster_size - skip;
        u64 chunk = length - done;
        if (chunk > available)
            chunk = available;
        if (bf_read_at(&fs->vol, base, dst + done, (u32)chunk) != 0)
            return -1;
        done += chunk;
        skip = 0;
        if (done >= length)
            break;
        int next = exfat_next(fs, cluster);
        if (next < 0)
            break;
        cluster = (u32)next;
    }
    if (read_out)
        *read_out = done;
    return done ? 0 : -1;
}

static int exfat_read(void *private, const char *rel, u64 offset, void *buffer,
                      u64 capacity, u64 *read_out)
{
    struct exfat *fs = private;
    char path[VFS_PATH_MAX];
    usize length = strlen(rel ? rel : "");
    if (length + 2 > sizeof(path))
        return -1;
    path[0] = '/';
    if (length)
        memcpy(path + 1, rel, length + 1);
    else
        path[1] = 0;

    struct exfat_node node;
    if (exfat_find(fs, path, &node) != 0 || node.is_dir)
        return -1;
    if (offset >= node.size) {
        if (read_out)
            *read_out = 0;
        return 0;
    }
    u64 count = node.size - offset;
    if (count > capacity)
        count = capacity;
    if (!count) {
        if (read_out)
            *read_out = 0;
        return 0;
    }

    u64 read = 0;
    u64 valid = node.valid_size > node.size ? node.size : node.valid_size;
    u64 valid_count = 0;
    if (offset < valid)
        valid_count = valid - offset;
    if (valid_count > count)
        valid_count = count;

    if (valid_count) {
        int result;
        if (node.nofat)
            result = exfat_read_contiguous(fs, node.cluster, offset,
                                           buffer, valid_count, &read);
        else
            result = exfat_read_chain(fs, node.cluster, offset, buffer,
                                      valid_count, &read);
        if (result != 0)
            return -1;
    }
    if (read < count)
        memset((u8 *)buffer + read, 0, (usize)(count - read));
    if (read_out)
        *read_out = count;
    return 0;
}

static const struct vfs_fs_ops g_exfat_ops = {
    .name = "exfat",
    .list = exfat_list,
    .stat = exfat_stat,
    .read = exfat_read,
    .write = NULL,
    .unlink = NULL,
};

static int exfat_parse_boot(const u8 *boot, struct exfat *fs)
{
    if (boot[0] != 0xEB && boot[0] != 0xE9)
        return -1;
    if (memcmp(boot + 3, "EXFAT   ", 8) != 0)
        return -1;
    u8 sector_shift = boot[108];
    u8 cluster_shift = boot[109];
    u8 fat_count = boot[110];
    if (sector_shift < 9 || sector_shift > 12)
        return -1;
    if (cluster_shift > 25)
        return -1;
    if (fat_count != 1 && fat_count != 2)
        return -1;

    u64 volume_length = bf_le64(boot + 72);
    u32 fat_offset = bf_le32(boot + 80);
    u32 fat_length = bf_le32(boot + 84);
    u32 heap_offset = bf_le32(boot + 88);
    u32 cluster_count = bf_le32(boot + 92);
    u32 root = bf_le32(boot + 96);

    u32 sector_size = 1u << sector_shift;
    if (!volume_length || !fat_offset || !fat_length || !heap_offset ||
        !cluster_count || root < 2 || root >= cluster_count + 2)
        return -1;
    if ((u64)heap_offset + (u64)cluster_count * (1u << cluster_shift) >
        volume_length)
        return -1;

    fs->sector_size = sector_size;
    fs->cluster_size = sector_size << cluster_shift;
    fs->fat_offset = fat_offset * sector_size;
    fs->heap_offset = heap_offset * sector_size;
    fs->cluster_count = cluster_count;
    fs->root_cluster = root;
    return 0;
}

int exfat_probe(const struct blockfs_volume *volume)
{
    if (!volume)
        return -1;
    u8 boot[512];
    if (bf_read_at(volume, 0, boot, sizeof(boot)) != 0)
        return -1;
    struct exfat fs;
    memset(&fs, 0, sizeof(fs));
    fs.vol = *volume;
    return exfat_parse_boot(boot, &fs) == 0 ? 0 : -1;
}

int exfat_mount_volume(const struct blockfs_volume *volume, const char *path)
{
    if (!volume || !path)
        return -1;
    u8 boot[512];
    if (bf_read_at(volume, 0, boot, sizeof(boot)) != 0)
        return -1;

    struct exfat *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return -1;
    fs->vol = *volume;
    if (exfat_parse_boot(boot, fs) != 0) {
        kfree(fs);
        return -1;
    }
    if (vfs_mount(path, "exfat", &g_exfat_ops, fs) != 0) {
        kfree(fs);
        return -1;
    }
    kprintf("[exfat] disk %d LBA %llu mounted at %s (%u clusters, %u-byte "
            "clusters)\n", fs->vol.disk, fs->vol.base, path,
            fs->cluster_count, fs->cluster_size);
    return 0;
}
