/*
 * Zhaoxu OS - read-only VFAT driver.
 *
 * Unlike the kernel's /mnt FAT32 writer, this driver is a general volume
 * reader: FAT12, FAT16 and FAT32, VFAT long names, the fixed root directory
 * of FAT12/16 and cluster chains for FAT32.  It is mounted on removable
 * volumes by the media probe, so it never assumes a label or a location.
 */
#include <blockfs.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define VFAT_NAME_MAX 256
#define VFAT_DIR_MAX (1024u * 1024u)
#define VFAT_CHAIN_MAX 65536u

struct vfat_entry {
    char name[VFAT_NAME_MAX];
    u32 cluster;
    u64 size;
    int is_dir;
};

struct vfat {
    struct blockfs_volume vol;
    u32 bps;
    u32 spc;
    u32 reserved;
    u32 fats;
    u32 fat_size;
    u32 root_entries;
    u32 total_sectors;
    u32 root_cluster;
    u32 cluster_count;
    u32 cluster_bytes;
    u64 fat_offset;
    u64 root_offset;
    u64 data_offset;
    int type;
};

/* Read the FAT entry for a cluster; FAT12 packs two entries into three
 * bytes, so a short unaligned read is the only portable way to get it. */
static u32 vfat_next(struct vfat *fs, u32 cluster)
{
    if (cluster < 2 || cluster + 2 > fs->cluster_count + 2)
        return 0x0FFFFFFFu;

    u8 bytes[4];
    if (fs->type == 12) {
        u32 offset = cluster * 3 / 2;
        if (bf_read_at(&fs->vol, fs->fat_offset + offset, bytes, 2) != 0)
            return 0x0FFFFFFFu;
        u16 value = bf_le16(bytes);
        return (cluster & 1) ? (value >> 4) : (value & 0x0FFFu);
    }
    if (fs->type == 16) {
        if (bf_read_at(&fs->vol, fs->fat_offset + cluster * 2, bytes, 2))
            return 0x0FFFFFFFu;
        return bf_le16(bytes);
    }
    if (bf_read_at(&fs->vol, fs->fat_offset + cluster * 4, bytes, 4))
        return 0x0FFFFFFFu;
    return bf_le32(bytes) & 0x0FFFFFFFu;
}

/* Follow a cluster chain and load the whole directory into one buffer.  A
 * directory larger than VFAT_DIR_MAX is rejected instead of exhausting the
 * kernel heap. */
static int vfat_load_dir(struct vfat *fs, u32 cluster, int fixed_root,
                         u8 **buffer, u64 *size)
{
    u64 capacity = fixed_root ? (u64)fs->root_entries * 32
                              : (u64)fs->cluster_bytes * 4;
    if (capacity == 0 || capacity > VFAT_DIR_MAX)
        return -1;

    u8 *data = kmalloc((usize)capacity);
    if (!data)
        return -1;
    u64 used = 0;

    if (fixed_root) {
        if (bf_read_at(&fs->vol, fs->root_offset, data,
                       (u32)capacity) != 0) {
            kfree(data);
            return -1;
        }
        *buffer = data;
        *size = capacity;
        return 0;
    }

    u32 current = cluster;
    for (u32 step = 0; step < VFAT_CHAIN_MAX; step++) {
        if (current < 2 || current >= fs->cluster_count + 2)
            break;
        if (used + fs->cluster_bytes > capacity) {
            u8 *grown = kmalloc((usize)(capacity * 2));
            if (!grown || capacity * 2 > VFAT_DIR_MAX) {
                kfree(grown);
                break;
            }
            memcpy(grown, data, (usize)used);
            kfree(data);
            data = grown;
            capacity *= 2;
        }
        u64 offset = fs->data_offset +
                     (u64)(current - 2) * fs->cluster_bytes;
        if (bf_read_at(&fs->vol, offset, data + used,
                       fs->cluster_bytes) != 0)
            break;
        used += fs->cluster_bytes;
        u32 next = vfat_next(fs, current);
        if (next == 0 || next >= 0x0FFFFFF8u || next == current)
            break;
        current = next;
    }

    if (!used) {
        kfree(data);
        return -1;
    }
    *buffer = data;
    *size = used;
    return 0;
}

/* Decode a short 8.3 name, honouring the two NT lowercase flags.  Entries
 * deleted or marked as volume labels are filtered by the caller. */
static void vfat_short_name(const u8 *entry, char *out, usize capacity)
{
    char base[9];
    char ext[4];
    usize used = 0;

    for (int i = 0; i < 8; i++) {
        char c = (char)entry[i];
        if (i == 0 && entry[0] == 0x05)
            c = (char)0xE5;
        base[i] = c;
    }
    base[8] = 0;
    for (int i = 0; i < 3; i++)
        ext[i] = (char)entry[8 + i];
    ext[3] = 0;

    for (int i = 7; i >= 0 && base[i] == ' '; i--)
        base[i] = 0;
    for (int i = 2; i >= 0 && ext[i] == ' '; i--)
        ext[i] = 0;

    int lowercase_base = (entry[12] & 0x08) != 0;
    int lowercase_ext = (entry[12] & 0x10) != 0;
    for (usize i = 0; base[i] && used + 1 < capacity; i++) {
        char c = base[i];
        if (lowercase_base && c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        out[used++] = c;
    }
    if (ext[0] && used + 1 < capacity) {
        out[used++] = '.';
        for (usize i = 0; ext[i] && used + 1 < capacity; i++) {
            char c = ext[i];
            if (lowercase_ext && c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
            out[used++] = c;
        }
    }
    out[used] = 0;
}

struct vfat_lfn {
    u16 units[VFAT_NAME_MAX];
    u32 highest;
    int active;
};

static void lfn_reset(struct vfat_lfn *lfn)
{
    memset(lfn, 0, sizeof(*lfn));
}

/* Collect the 13 UTF-16 characters a long-name slot carries. */
static void lfn_feed(struct vfat_lfn *lfn, const u8 *entry)
{
    u8 order = entry[0];
    if (order == 0xE5)
        return;
    u32 index = (u32)(order & 0x1F);
    if (!index || index > 20) {
        lfn->active = 0;
        return;
    }
    if (order & 0x40) {
        lfn_reset(lfn);
        lfn->active = 1;
        lfn->highest = index;
    } else if (!lfn->active) {
        return;
    }

    static const u8 offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18,
                                   20, 22, 24, 28, 30};
    u32 base = (index - 1) * 13;
    for (u32 i = 0; i < 13; i++) {
        u32 slot = base + i;
        if (slot + 1 >= VFAT_NAME_MAX)
            break;
        lfn->units[slot] = bf_le16(entry + offsets[i]);
    }
}

/* Emit one directory entry.  Returns 1 when the caller should stop scanning
 * (end-of-directory marker), 0 otherwise. */
static int vfat_parse_entry(struct vfat *fs, const u8 *entry,
                            struct vfat_lfn *lfn, struct vfat_entry *out)
{
    u8 attr = entry[11];
    memset(out, 0, sizeof(*out));
    if (entry[0] == 0x00)
        return 1;
    if (entry[0] == 0xE5) {
        lfn_reset(lfn);
        return 0;
    }
    if (attr == 0x0F) {
        lfn_feed(lfn, entry);
        return 0;
    }
    if (attr & 0x08) {
        lfn_reset(lfn);
        return 0;
    }

    memset(out, 0, sizeof(*out));
    if (lfn->active && lfn->highest) {
        u32 units = lfn->highest * 13;
        if (units > VFAT_NAME_MAX)
            units = VFAT_NAME_MAX;
        bf_utf16_to_utf8(lfn->units, units, out->name,
                         sizeof(out->name));
    }
    if (!out->name[0])
        vfat_short_name(entry, out->name, sizeof(out->name));
    lfn_reset(lfn);
    if (!out->name[0])
        return 0;

    out->cluster = (u32)bf_le16(entry + 26) |
                   ((u32)bf_le16(entry + 20) << 16);
    out->size = bf_le32(entry + 28);
    out->is_dir = (attr & 0x10) != 0;
    (void)fs;
    return 0;
}

static int vfat_dir_entry_count(struct vfat *fs, u32 cluster, int fixed,
                                u8 **data_out, u64 *size_out)
{
    return vfat_load_dir(fs, cluster, fixed, data_out, size_out);
}

static int vfat_find(struct vfat *fs, const char *path,
                     struct vfat_entry *result)
{
    if (!path || path[0] != '/')
        return -1;

    u32 cluster = fs->type == 32 ? fs->root_cluster : 0;
    int fixed = fs->type != 32;
    if (!path[1]) {
        memset(result, 0, sizeof(*result));
        strcpy(result->name, "/");
        result->cluster = cluster;
        result->is_dir = 1;
        return 0;
    }

    const char *cursor = path + 1;
    while (*cursor) {
        char component[VFAT_NAME_MAX];
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
        if (vfat_dir_entry_count(fs, cluster, fixed, &data, &size) != 0)
            return -1;

        struct vfat_entry found;
        int matched = 0;
        struct vfat_lfn lfn;
        lfn_reset(&lfn);
        for (u64 offset = 0; offset + 32 <= size; offset += 32) {
            if (vfat_parse_entry(fs, data + offset, &lfn, &found))
                break;
            if (!found.name[0] || strcmp(found.name, ".") == 0 ||
                strcmp(found.name, "..") == 0)
                continue;
            if (bf_name_equal(found.name, component)) {
                matched = 1;
                break;
            }
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
        fixed = 0;
    }
    return -1;
}

static int vfat_list(void *private, const char *rel, vfs_list_cb callback,
                     void *context)
{
    struct vfat *fs = private;
    u32 cluster = fs->type == 32 ? fs->root_cluster : 0;
    int fixed = fs->type != 32;

    if (rel && rel[0]) {
        char path[VFS_PATH_MAX];
        usize length = strlen(rel);
        if (length + 2 > sizeof(path))
            return -1;
        path[0] = '/';
        memcpy(path + 1, rel, length + 1);
        struct vfat_entry entry;
        if (vfat_find(fs, path, &entry) != 0 || !entry.is_dir)
            return -1;
        cluster = entry.cluster;
        fixed = 0;
    }

    u8 *data = NULL;
    u64 size = 0;
    if (vfat_dir_entry_count(fs, cluster, fixed, &data, &size) != 0)
        return -1;

    struct vfat_lfn lfn;
    lfn_reset(&lfn);
    for (u64 offset = 0; offset + 32 <= size; offset += 32) {
        struct vfat_entry entry;
        if (vfat_parse_entry(fs, data + offset, &lfn, &entry))
            break;
        if (!entry.name[0] || strcmp(entry.name, ".") == 0 ||
            strcmp(entry.name, "..") == 0)
            continue;
        callback(entry.name, entry.size, entry.is_dir, context);
    }
    kfree(data);
    return 0;
}

static int vfat_stat(void *private, const char *rel, int *is_dir, u64 *size)
{
    struct vfat *fs = private;
    char path[VFS_PATH_MAX];
    usize length = strlen(rel ? rel : "");
    if (length + 2 > sizeof(path))
        return -1;
    path[0] = '/';
    if (length)
        memcpy(path + 1, rel, length + 1);
    else
        path[1] = 0;

    struct vfat_entry entry;
    if (vfat_find(fs, path, &entry) != 0)
        return -1;
    if (is_dir)
        *is_dir = entry.is_dir;
    if (size)
        *size = entry.size;
    return 0;
}

/* Copy bytes from a cluster chain, walking only the clusters the requested
 * range actually touches. */
static int vfat_read_chain(struct vfat *fs, u32 start, u64 offset, u8 *dst,
                           u64 length, u64 *read_out)
{
    u32 cluster = start;
    u64 skip = offset;
    u64 done = 0;

    if (read_out)
        *read_out = 0;
    if (!length)
        return 0;
    if (start < 2)
        return -1;

    for (u32 step = 0; step < VFAT_CHAIN_MAX && skip >= fs->cluster_bytes;
         step++) {
        u32 next = vfat_next(fs, cluster);
        if (next < 2 || next >= 0x0FFFFFF8u || next == cluster)
            return -1;
        cluster = next;
        skip -= fs->cluster_bytes;
    }

    for (u32 step = 0; step < VFAT_CHAIN_MAX && done < length; step++) {
        if (cluster < 2 || cluster >= fs->cluster_count + 2)
            break;
        u64 base = fs->data_offset +
                   (u64)(cluster - 2) * fs->cluster_bytes + skip;
        u64 available = fs->cluster_bytes - skip;
        u64 chunk = length - done;
        if (chunk > available)
            chunk = available;
        if (bf_read_at(&fs->vol, base, dst + done, (u32)chunk) != 0)
            return -1;
        done += chunk;
        skip = 0;
        if (done >= length)
            break;
        u32 next = vfat_next(fs, cluster);
        if (next < 2 || next >= 0x0FFFFFF8u || next == cluster)
            break;
        cluster = next;
    }
    if (read_out)
        *read_out = done;
    return done ? 0 : -1;
}

static int vfat_read(void *private, const char *rel, u64 offset, void *buffer,
                     u64 capacity, u64 *read_out)
{
    struct vfat *fs = private;
    char path[VFS_PATH_MAX];
    usize length = strlen(rel ? rel : "");
    if (length + 2 > sizeof(path))
        return -1;
    path[0] = '/';
    if (length)
        memcpy(path + 1, rel, length + 1);
    else
        path[1] = 0;

    struct vfat_entry entry;
    if (vfat_find(fs, path, &entry) != 0 || entry.is_dir)
        return -1;
    if (offset >= entry.size) {
        if (read_out)
            *read_out = 0;
        return 0;
    }
    u64 count = entry.size - offset;
    if (count > capacity)
        count = capacity;
    if (!count) {
        if (read_out)
            *read_out = 0;
        return 0;
    }
    return vfat_read_chain(fs, entry.cluster, offset, buffer, count,
                           read_out);
}

static const struct vfs_fs_ops g_vfat_ops = {
    .name = "vfat",
    .list = vfat_list,
    .stat = vfat_stat,
    .read = vfat_read,
    .write = NULL,
    .unlink = NULL,
};

/* Validate a BPB and reject the two foreign formats that share FAT's boot
 * sector area (NTFS and exFAT). */
static int vfat_parse_bpb(const u8 *boot, struct vfat *fs)
{
    if (memcmp(boot + 3, "NTFS    ", 8) == 0 ||
        memcmp(boot + 3, "EXFAT   ", 8) == 0)
        return -1;
    if (boot[510] != 0x55 || boot[511] != 0xAA)
        return -1;

    u32 bps = bf_le16(boot + 11);
    u32 spc = boot[13];
    u32 reserved = bf_le16(boot + 14);
    u32 fats = boot[16];
    u32 root_entries = bf_le16(boot + 17);
    u32 total = bf_le16(boot + 19);
    u32 fat16 = bf_le16(boot + 22);
    if (!total)
        total = bf_le32(boot + 32);
    u32 fat32 = bf_le32(boot + 36);

    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096)
        return -1;
    if (!spc || (spc & (spc - 1)))
        return -1;
    if (!reserved || !fats || fats > 4 || !total)
        return -1;
    u32 fat_size = fat16 ? fat16 : fat32;
    if (!fat_size)
        return -1;

    u32 root_dir_sectors =
        ((root_entries * 32) + bps - 1) / bps;
    u32 first_data = reserved + fats * fat_size + root_dir_sectors;
    if (first_data >= total)
        return -1;
    u32 clusters = (total - first_data) / spc;
    if (!clusters)
        return -1;

    fs->bps = bps;
    fs->spc = spc;
    fs->reserved = reserved;
    fs->fats = fats;
    fs->fat_size = fat_size;
    fs->root_entries = root_entries;
    fs->total_sectors = total;
    fs->cluster_count = clusters;
    fs->cluster_bytes = spc * bps;
    fs->fat_offset = (u64)reserved * bps;
    fs->root_offset = (u64)(reserved + fats * fat_size) * bps;
    fs->data_offset = (u64)first_data * bps;

    if (clusters < 4085)
        fs->type = 12;
    else if (clusters < 65525)
        fs->type = 16;
    else
        fs->type = 32;

    if (fs->type == 32) {
        fs->root_cluster = bf_le32(boot + 44);
        if (fs->root_cluster < 2 || fs->root_cluster >= clusters + 2)
            return -1;
    } else {
        fs->root_cluster = 0;
        if (!root_entries)
            return -1;
    }
    /* The volume must be large enough for at least the FAT and root area. */
    u64 bytes = (u64)total * bps;
    if (bytes < fs->data_offset + (u64)fs->cluster_bytes)
        return -1;
    return 0;
}

int vfat_probe(const struct blockfs_volume *volume)
{
    if (!volume)
        return -1;
    u8 boot[512];
    if (bf_read_at(volume, 0, boot, sizeof(boot)) != 0)
        return -1;

    struct vfat fs;
    memset(&fs, 0, sizeof(fs));
    fs.vol = *volume;
    fs.vol.logical = 512;
    fs.vol.stride = 512;
    fs.vol.data_offset = 0;
    return vfat_parse_bpb(boot, &fs) == 0 ? 0 : -1;
}

int vfat_mount_volume(const struct blockfs_volume *volume, const char *path)
{
    if (!volume || !path)
        return -1;
    u8 boot[512];
    if (bf_read_at(volume, 0, boot, sizeof(boot)) != 0)
        return -1;

    struct vfat *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return -1;
    fs->vol = *volume;
    fs->vol.logical = 512;
    fs->vol.stride = 512;
    fs->vol.data_offset = 0;
    if (vfat_parse_bpb(boot, fs) != 0) {
        kfree(fs);
        return -1;
    }
    if (vfs_mount(path, "vfat", &g_vfat_ops, fs) != 0) {
        kfree(fs);
        return -1;
    }
    kprintf("[vfat] FAT%u on disk %d LBA %llu mounted at %s (%u clusters)\n",
            (unsigned)fs->type, fs->vol.disk, fs->vol.base, path,
            fs->cluster_count);
    return 0;
}
