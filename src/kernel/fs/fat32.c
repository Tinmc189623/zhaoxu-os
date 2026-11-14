/*
 * Zhaoxu OS - FAT32 filesystem: mount, lookup, list and read
 *
 * The driver scans the AHCI block devices for a FAT32 volume (either a
 * superfloppy or the first partition of an MBR disk), preferring the volume
 * labelled ZHAOXUDATA.  Directory entries support VFAT long file names.
 */
#include <block.h>
#include <fat32.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>

#include "fat32_internal.h"

static struct fat32_fs g_fs;

u16 read_le16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

u32 read_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 read_le64(const u8 *p)
{
    return (u64)read_le32(p) | ((u64)read_le32(p + 4) << 32);
}

int name_equal(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++;
        char y = *b++;
        if (x >= 'a' && x <= 'z')
            x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z')
            y = (char)(y - 'a' + 'A');
        if (x != y)
            return 0;
    }
    return *a == 0 && *b == 0;
}

static void trim_label(char *label)
{
    for (int i = 10; i >= 0; i--) {
        if (label[i] == ' ' || label[i] == 0)
            label[i] = 0;
        else
            break;
    }
}

static int is_fat32_boot(const u8 *sector, char *label_out)
{
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return 0;

    u32 bytes_per_sector = read_le16(sector + 11);
    u32 sectors_per_cluster = sector[13];
    u32 reserved = read_le16(sector + 14);
    u32 fats = sector[16];
    u32 fat_size = read_le32(sector + 36);
    u32 total = read_le32(sector + 32);
    u32 root_cluster = read_le32(sector + 44);

    if (bytes_per_sector != 512)
        return 0;
    if (!sectors_per_cluster ||
        (sectors_per_cluster & (sectors_per_cluster - 1)))
        return 0;
    if (!reserved || !fats || !fat_size || !total)
        return 0;
    if (root_cluster < 2)
        return 0;
    if (memcmp(sector + 82, "FAT32   ", 8) != 0)
        return 0;

    char label[FAT32_LABEL_MAX];
    memset(label, 0, sizeof(label));
    if (sector[66] == 0x29)
        memcpy(label, sector + 71, 11);
    trim_label(label);
    if (label_out)
        strcpy(label_out, label);
    return 1;
}

int read_sector(u64 lba, void *buffer)
{
    return block_disk_read(g_fs.disk, g_fs.volume_base + lba, 1, buffer);
}

int read_cluster(u32 cluster, void *buffer)
{
    u64 lba = (u64)g_fs.data_start +
              (u64)(cluster - 2) * g_fs.sectors_per_cluster;
    return block_disk_read(g_fs.disk, g_fs.volume_base + lba,
                          g_fs.sectors_per_cluster, buffer);
}

u32 fat_next_cluster(u32 cluster)
{
    u32 offset = cluster * 4;
    u32 sector = g_fs.reserved_sectors + offset / 512;
    u32 index = (offset % 512) / 4;

    if (!g_fs.cache_valid || g_fs.cache_sector != sector) {
        if (read_sector(sector, g_fs.cache) != 0)
            return 0xFFFFFFFF;
        g_fs.cache_sector = sector;
        g_fs.cache_valid = 1;
    }

    u32 value = read_le32(g_fs.cache + index * 4) & 0x0FFFFFFF;
    if (value == 0 || value >= 0x0FFFFFF8)
        return 0xFFFFFFFF;
    return value;
}

struct lfn_state {
    int active;
    u32 highest;
    u16 chars[260];
};

static void lfn_reset(struct lfn_state *state)
{
    state->active = 0;
    state->highest = 0;
}

static void lfn_feed(struct lfn_state *state, const u8 *entry)
{
    u32 sequence = entry[0] & 0x1F;
    if (entry[0] & 0x40) {
        memset(state->chars, 0, sizeof(state->chars));
        state->active = 1;
        state->highest = sequence;
    }
    if (!state->active || sequence == 0 || sequence > 20)
        return;

    static const u8 offsets[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    u32 base = (sequence - 1) * 13;
    for (u32 i = 0; i < 13; i++) {
        u32 target = base + i;
        if (target >= 259)
            break;
        state->chars[target] =
            (u16)(entry[offsets[i]] | ((u16)entry[offsets[i] + 1] << 8));
    }
}

static void lfn_to_ascii(const struct lfn_state *state, char *output)
{
    u32 length = state->highest * 13;
    if (length > 259)
        length = 259;
    u32 out = 0;
    for (u32 i = 0; i < length; i++) {
        u16 character = state->chars[i];
        if (character == 0 || character == 0xFFFF)
            break;
        output[out++] = character < 0x80 ? (char)character : '?';
    }
    output[out] = 0;
}

void short_name(const u8 *entry, char *output)
{
    u32 out = 0;
    for (u32 i = 0; i < 8 && entry[i] != ' '; i++)
        output[out++] = (char)entry[i];
    u32 extension = 0;
    for (u32 i = 8; i < 11; i++) {
        if (entry[i] != ' ')
            extension++;
    }
    if (extension) {
        output[out++] = '.';
        for (u32 i = 8; i < 11 && entry[i] != ' '; i++)
            output[out++] = (char)entry[i];
    }
    output[out] = 0;
}

struct dir_state {
    int (*callback)(const struct fat_entry *entry, void *context);
    void *context;
    struct lfn_state lfn;
};

static int process_dir_entry(struct dir_state *state, const u8 *entry)
{
    if (entry[0] == 0x00)
        return 1;
    if (entry[0] == 0xE5) {
        lfn_reset(&state->lfn);
        return 0;
    }

    u8 attributes = entry[11];
    if (attributes == 0x0F) {
        lfn_feed(&state->lfn, entry);
        return 0;
    }

    if (attributes & 0x08) {
        lfn_reset(&state->lfn);
        return 0;
    }

    if (entry[0] == '.' && (entry[1] == ' ' || entry[1] == '.')) {
        lfn_reset(&state->lfn);
        return 0;
    }

    struct fat_entry item;
    memset(&item, 0, sizeof(item));
    if (state->lfn.active && state->lfn.highest > 0)
        lfn_to_ascii(&state->lfn, item.name);
    else
        short_name(entry, item.name);

    item.is_dir = (attributes & 0x10) != 0;
    item.cluster = ((u32)read_le16(entry + 20) << 16) |
                   read_le16(entry + 26);
    item.size = read_le32(entry + 28);

    lfn_reset(&state->lfn);

    if (item.name[0] == 0)
        return 0;
    return state->callback(&item, state->context);
}

static int iterate_directory(u32 start_cluster,
                             int (*callback)(const struct fat_entry *, void *),
                             void *context)
{
    if (start_cluster < 2)
        return 0;

    u32 cluster_bytes = g_fs.sectors_per_cluster * 512;
    u8 *buffer = kmalloc(cluster_bytes);
    if (!buffer)
        return -1;

    struct dir_state state;
    memset(&state, 0, sizeof(state));
    state.callback = callback;
    state.context = context;

    u32 cluster = start_cluster;
    u32 guard = g_fs.cluster_count + 2;

    while (cluster >= 2 && guard-- > 0) {
        if (read_cluster(cluster, buffer) != 0) {
            kfree(buffer);
            return -1;
        }
        for (u32 offset = 0; offset < cluster_bytes; offset += 32) {
            int result = process_dir_entry(&state, buffer + offset);
            if (result != 0) {
                kfree(buffer);
                return result > 0 ? 0 : -1;
            }
        }
        cluster = fat_next_cluster(cluster);
    }

    kfree(buffer);
    return 0;
}

struct lookup_state {
    const char *name;
    int found;
    struct fat_entry entry;
};

static int lookup_callback(const struct fat_entry *entry, void *context)
{
    struct lookup_state *state = (struct lookup_state *)context;
    if (name_equal(entry->name, state->name)) {
        state->entry = *entry;
        state->found = 1;
        return 1;
    }
    return 0;
}

static int split_component(const char **path, char *output, usize capacity,
                           int *last)
{
    const char *cursor = *path;
    while (*cursor == '/')
        cursor++;
    if (!*cursor) {
        *path = cursor;
        return 0;
    }

    usize length = 0;
    while (*cursor && *cursor != '/') {
        if (length + 1 < capacity)
            output[length] = *cursor;
        length++;
        cursor++;
    }
    if (length >= capacity)
        return -1;
    output[length] = 0;

    while (*cursor == '/')
        cursor++;
    *last = (*cursor == 0);
    *path = cursor;
    return 1;
}

int fat32_find(const char *path, struct fat_entry *result)
{
    if (!g_fs.mounted)
        return -1;

    memset(result, 0, sizeof(*result));

    const char *cursor = path ? path : "/";
    while (*cursor == '/')
        cursor++;
    if (!*cursor) {
        result->is_dir = 1;
        result->cluster = g_fs.root_cluster;
        strcpy(result->name, "/");
        return 0;
    }

    u32 directory = g_fs.root_cluster;
    int directory_is_root = 1;
    (void)directory_is_root;

    for (;;) {
        char component[256];
        int last = 0;
        int status = split_component(&cursor, component, sizeof(component),
                                     &last);
        if (status <= 0)
            return -1;

        struct lookup_state lookup;
        memset(&lookup, 0, sizeof(lookup));
        lookup.name = component;
        if (iterate_directory(directory, lookup_callback, &lookup) != 0)
            return -1;
        if (!lookup.found)
            return -1;

        if (last) {
            *result = lookup.entry;
            return 0;
        }
        if (!lookup.entry.is_dir)
            return -1;
        if (lookup.entry.cluster < 2)
            return -1;
        directory = lookup.entry.cluster;
    }
}

static int list_callback(const struct fat_entry *entry, void *context)
{
    struct {
        vfs_list_cb callback;
        void *context;
    } *state = (void *)context;
    state->callback(entry->name, entry->size, entry->is_dir, state->context);
    return 0;
}

int fat32_list(const char *path, vfs_list_cb callback, void *context)
{
    struct fat_entry entry;
    if (fat32_find(path, &entry) != 0 || !entry.is_dir)
        return -1;

    struct {
        vfs_list_cb callback;
        void *context;
    } state = { callback, context };

    return iterate_directory(entry.cluster, list_callback, &state);
}

static int fat32_read_data(const struct fat_entry *entry, u8 *data, u64 size)
{
    u32 cluster_bytes = g_fs.sectors_per_cluster * 512;
    u8 *cluster_buffer = kmalloc(cluster_bytes);
    if (!cluster_buffer)
        return -1;

    u32 cluster = entry->cluster;
    u32 remaining = (u32)size;
    u32 guard = g_fs.cluster_count + 2;
    u8 *output = data;

    while (remaining > 0 && cluster >= 2 && guard-- > 0) {
        u64 lba = (u64)g_fs.data_start +
                  (u64)(cluster - 2) * g_fs.sectors_per_cluster;
        if (block_disk_read(g_fs.disk, g_fs.volume_base + lba,
                           g_fs.sectors_per_cluster, cluster_buffer) != 0) {
            kfree(cluster_buffer);
            return -1;
        }
        u32 chunk = remaining > cluster_bytes ? cluster_bytes : remaining;
        memcpy(output, cluster_buffer, chunk);
        output += chunk;
        remaining -= chunk;
        cluster = fat_next_cluster(cluster);
    }

    kfree(cluster_buffer);

    if (remaining > 0)
        memset(output, 0, remaining);
    return 0;
}

int fat32_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out)
{
    struct fat_entry entry;
    if (!dst || fat32_find(path, &entry) != 0 || entry.is_dir)
        return -1;
    if (entry.size > cap)
        return -1;
    if (fat32_read_data(&entry, dst, entry.size) != 0)
        return -1;
    if (read_out)
        *read_out = entry.size;
    return 0;
}

/* Read a byte range without copying the whole file.  The FAT chain is walked
 * to the cluster containing the offset, then clusters are streamed into a
 * reusable buffer.  Used by the VFS file-descriptor path. */
int fat32_read_at(const char *path, u64 offset, u8 *dst, u64 cap,
                  u64 *read_out)
{
    struct fat_entry entry;
    if ((!dst && cap) || fat32_find(path, &entry) != 0 || entry.is_dir)
        return -1;
    if (read_out)
        *read_out = 0;
    if (offset >= entry.size)
        return 0;

    u32 cluster_bytes = g_fs.sectors_per_cluster * 512;
    if (!cluster_bytes)
        return -1;
    u64 skip = offset / cluster_bytes;
    u32 within = (u32)(offset % cluster_bytes);
    u32 cluster = entry.cluster;
    u32 guard = g_fs.cluster_count + 2;
    while (skip > 0 && cluster >= 2 && guard-- > 0) {
        cluster = fat_next_cluster(cluster);
        skip--;
    }
    if (cluster < 2 || guard == 0)
        return -1;

    u64 available = entry.size - offset;
    u64 want = cap < available ? cap : available;
    u8 *cluster_buffer = kmalloc(cluster_bytes);
    if (!cluster_buffer)
        return -1;

    u64 copied = 0;
    guard = g_fs.cluster_count + 2;
    while (copied < want && cluster >= 2 && guard-- > 0) {
        u64 lba = (u64)g_fs.data_start +
                  (u64)(cluster - 2) * g_fs.sectors_per_cluster;
        if (block_disk_read(g_fs.disk, g_fs.volume_base + lba,
                            g_fs.sectors_per_cluster, cluster_buffer) != 0) {
            kfree(cluster_buffer);
            return -1;
        }
        u32 start = within;
        u32 chunk = cluster_bytes - start;
        if ((u64)chunk > want - copied)
            chunk = (u32)(want - copied);
        memcpy(dst + copied, cluster_buffer + start, chunk);
        copied += chunk;
        within = 0;
        if (copied < want)
            cluster = fat_next_cluster(cluster);
    }

    kfree(cluster_buffer);
    if (copied != want)
        return -1;
    if (read_out)
        *read_out = copied;
    return 0;
}

int fat32_read_all(const char *path, u8 **buffer, u64 *size)
{
    struct fat_entry entry;
    if (fat32_find(path, &entry) != 0 || entry.is_dir)
        return -1;
    if (entry.size > FAT32_MAX_FILE_SIZE)
        return -1;

    u8 *data = kmalloc(entry.size ? entry.size : 1);
    if (!data)
        return -1;
    if (fat32_read_data(&entry, data, entry.size) != 0) {
        kfree(data);
        return -1;
    }

    *buffer = data;
    if (size)
        *size = entry.size;
    return 0;
}

int fat32_stat(const char *path, int *is_dir, u64 *size)
{
    struct fat_entry entry;
    if (fat32_find(path, &entry) != 0)
        return -1;
    if (is_dir)
        *is_dir = entry.is_dir;
    if (size)
        *size = entry.size;
    return 0;
}

static void load_bpb(const u8 *sector, u64 volume_base, int disk,
                     const char *label)
{
    memset(&g_fs, 0, sizeof(g_fs));
    g_fs.disk = disk;
    g_fs.volume_base = volume_base;
    g_fs.bytes_per_sector = read_le16(sector + 11);
    g_fs.sectors_per_cluster = sector[13];
    g_fs.reserved_sectors = read_le16(sector + 14);
    g_fs.num_fats = sector[16];
    g_fs.fat_size = read_le32(sector + 36);
    g_fs.root_cluster = read_le32(sector + 44);
    g_fs.total_sectors = read_le32(sector + 32);
    g_fs.data_start = g_fs.reserved_sectors + g_fs.num_fats * g_fs.fat_size;
    u32 fsinfo = read_le16(sector + 48);
    g_fs.fsinfo_sector = fsinfo > 0 && fsinfo < g_fs.reserved_sectors ? fsinfo : 0;

    u32 data_sectors = g_fs.total_sectors > g_fs.data_start
                           ? g_fs.total_sectors - g_fs.data_start
                           : 0;
    g_fs.cluster_count = data_sectors / g_fs.sectors_per_cluster;
    strncpy(g_fs.label, label, FAT32_LABEL_MAX - 1);
    g_fs.label[FAT32_LABEL_MAX - 1] = 0;
    g_fs.mounted = 1;
}

int fat32_mount(void)
{
    int disk_count = block_disk_count();
    if (disk_count <= 0) {
        kprintf("[fat32] no block device available\n");
        return -1;
    }

    u8 sector[512];
    u8 boot[512];
    int best_disk = -1;
    u64 best_base = 0;
    char best_label[FAT32_LABEL_MAX];
    int best_score = 0;
    memset(best_label, 0, sizeof(best_label));

    for (int disk = 0; disk < disk_count; disk++) {
        u64 sectors = block_disk_sectors(disk);
        if (sectors < 2) {
            kprintf("[fat32] disk %d: only %llu sectors\n", disk, sectors);
            continue;
        }
        if (block_disk_read(disk, 0, 1, sector) != 0) {
            kprintf("[fat32] disk %d: sector 0 read failed\n", disk);
            continue;
        }

        u64 candidates[8];
        char labels[8][FAT32_LABEL_MAX];
        int candidate_count = 0;

        char label[FAT32_LABEL_MAX];
        label[0] = 0;
        if (is_fat32_boot(sector, label)) {
            candidates[candidate_count] = 0;
            strcpy(labels[candidate_count], label);
            candidate_count++;
        }

        if ((sector[510] == 0x55 && sector[511] == 0xAA) &&
            candidate_count == 0) {
            for (int partition = 0; partition < 4; partition++) {
                const u8 *entry = sector + 446 + partition * 16;
                u8 type = entry[4];
                if (type != 0x0B && type != 0x0C && type != 0x0E &&
                    type != 0x06 && type != 0x04 && type != 0x01)
                    continue;
                u64 start = read_le32(entry + 8);
                if (start == 0 || start >= sectors)
                    continue;
                if (block_disk_read(disk, start, 1, boot) != 0)
                    continue;
                label[0] = 0;
                if (!is_fat32_boot(boot, label))
                    continue;
                candidates[candidate_count] = start;
                strcpy(labels[candidate_count], label);
                candidate_count++;
                if (candidate_count >= 8)
                    break;
            }
        }

        if (candidate_count == 0 && sector[446 + 4] == 0xEE &&
            block_disk_read(disk, 1, 1, boot) == 0 &&
            memcmp(boot, "EFI PART", 8) == 0) {
            u64 entries_lba = read_le64(boot + 72);
            u32 entries = read_le32(boot + 80);
            u32 entry_size = read_le32(boot + 84);
            if (entry_size >= 128 && entries <= 128 && entries_lba < sectors) {
                for (u32 i = 0; i < entries && candidate_count < 8; i++) {
                    u64 offset = (u64)i * entry_size;
                    u32 within = (u32)(offset % 512);
                    if (within + 128 > 512)
                        continue;
                    if (block_disk_read(disk, entries_lba + offset / 512, 1,
                                       boot) != 0)
                        continue;
                    const u8 *entry = boot + within;
                    int used = 0;
                    for (int k = 0; k < 16; k++)
                        used |= entry[k];
                    if (!used)
                        continue;
                    u64 start = read_le64(entry + 32);
                    if (start == 0 || start >= sectors)
                        continue;
                    if (block_disk_read(disk, start, 1, boot) != 0)
                        continue;
                    label[0] = 0;
                    if (!is_fat32_boot(boot, label))
                        continue;
                    candidates[candidate_count] = start;
                    strcpy(labels[candidate_count], label);
                    candidate_count++;
                }
            }
        }

        kprintf("[fat32] disk %d: %d FAT candidate(s)", disk, candidate_count);
        if (candidate_count > 0)
            kprintf(", first \"%s\"", labels[0]);
        kprintf("\n");

        for (int i = 0; i < candidate_count; i++) {
            int score = name_equal(labels[i], "ZHAOXUDATA") ? 2 : 1;
            if (score > best_score) {
                best_score = score;
                best_disk = disk;
                best_base = candidates[i];
                strcpy(best_label, labels[i]);
            }
        }
    }

    if (best_disk < 0) {
        kprintf("[fat32] no FAT32 volume found\n");
        return -1;
    }

    if (block_disk_read(best_disk, best_base, 1, boot) != 0)
        return -1;

    load_bpb(boot, best_base, best_disk, best_label);
    g_fs.cache_valid = 0;

    kprintf("[fat32] mounted \"%s\" on /mnt: disk %d, %u sectors/cluster, "
            "%u clusters\n",
            g_fs.label, best_disk, g_fs.sectors_per_cluster,
            g_fs.cluster_count);
    return 0;
}

struct fat32_fs *fat32_state(void)
{
    return &g_fs;
}

int fat32_mount_at(int disk, u64 volume_base)
{
    u8 sector[512];
    if (block_disk_read(disk, volume_base, 1, sector) != 0)
        return -1;
    char label[FAT32_LABEL_MAX];
    if (!is_fat32_boot(sector, label))
        return -1;
    load_bpb(sector, volume_base, disk, label);
    g_fs.cache_valid = 0;
    return 0;
}

int fat32_mounted(void)
{
    return g_fs.mounted;
}

const char *fat32_label(void)
{
    return g_fs.mounted ? g_fs.label : NULL;
}
