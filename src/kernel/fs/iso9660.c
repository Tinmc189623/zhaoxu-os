/*
 * Zhaoxu OS - ISO 9660 / CDFS reader.
 *
 * Handles primary and Joliet volume descriptors, multi-extent files and the
 * Rock Ridge NM name extension.  The same parser serves both a 2048-byte
 * data-track CD and a raw 2352-byte mode-1 sector stream; the volume layer
 * hides the sector layout.
 */
#include <blockfs.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define ISO_BLOCK 2048u
#define ISO_NAME_MAX 256
#define ISO_EXTENTS 32
#define ISO_DIR_MAX (8u * 1024u * 1024u)

struct iso_extent {
    u32 lba;
    u32 size;
};

struct iso_node {
    char name[ISO_NAME_MAX];
    u64 size;
    int is_dir;
    int extent_count;
    struct iso_extent extents[ISO_EXTENTS];
};

struct iso9660 {
    struct blockfs_volume vol;
    u32 block_size;
    int joliet;
    u32 root_lba;
    u32 root_size;
    u32 susp_skip;
    int susp;
};

struct iso_record {
    u32 lba;
    u32 size;
    u8 flags;
    const u8 *name;
    u32 name_len;
    const u8 *susp;
    u32 susp_len;
};

static u32 iso_le32(const u8 *p)
{
    return bf_le32(p);
}

static void iso_utf16be(const u8 *source, u32 bytes, char *out,
                        u32 capacity)
{
    u16 units[ISO_NAME_MAX];
    u32 count = bytes / 2;
    if (count >= ISO_NAME_MAX)
        count = ISO_NAME_MAX - 1;
    for (u32 i = 0; i < count; i++)
        units[i] = (u16)((source[i * 2] << 8) | source[i * 2 + 1]);
    bf_utf16_to_utf8(units, count, out, capacity);
}

/* Copy an ISO identifier, dropping the ";1" version and the trailing dot. */
static void iso_ascii_name(const u8 *source, u32 length, char *out,
                           u32 capacity)
{
    u32 used = 0;
    for (u32 i = 0; i < length && used + 1 < capacity; i++) {
        u8 c = source[i];
        if (c == ';')
            break;
        out[used++] = (char)c;
    }
    while (used && out[used - 1] == '.')
        used--;
    out[used] = 0;
}

/* Extract the Rock Ridge NM name from a system-use area.  Multiple NM
 * entries in one record are concatenated as SUSP requires. */
static int iso_rock_ridge_name(const u8 *susp, u32 length, char *out,
                               u32 capacity)
{
    u32 used = 0;
    u32 offset = 0;
    while (offset + 4 <= length) {
        u8 entry_len = susp[offset + 2];
        if (entry_len < 4 || offset + entry_len > length)
            break;
        const u8 *entry = susp + offset;
        if (entry[0] == 'N' && entry[1] == 'M' && entry_len >= 5) {
            u8 flags = entry[4];
            if (flags & 0x02) {
                if (used + 1 < capacity)
                    out[used++] = '.';
                offset += entry_len;
                continue;
            }
            if (flags & 0x04) {
                if (used + 2 < capacity) {
                    out[used++] = '.';
                    out[used++] = '.';
                }
                offset += entry_len;
                continue;
            }
            if (flags & 0x08) {
                if (used + 1 < capacity)
                    out[used++] = '/';
                offset += entry_len;
                continue;
            }
            u32 count = entry_len - 5;
            if (count > capacity - used - 1)
                count = capacity - used - 1;
            memcpy(out + used, entry + 5, count);
            used += count;
        }
        offset += entry_len;
    }
    if (used) {
        out[used] = 0;
        return 1;
    }
    return 0;
}

/* Return 1 when a complete record was decoded, 0 at the end of the
 * directory and -1 for a malformed record. */
static int iso_next_record(struct iso9660 *fs, const u8 *dir, u32 dir_len,
                           u32 *cursor, struct iso_record *record)
{
    while (*cursor < dir_len) {
        u32 within = *cursor % fs->block_size;
        if (within + 33 > fs->block_size) {
            *cursor += fs->block_size - within;
            continue;
        }
        u32 length = dir[*cursor];
        if (length == 0) {
            *cursor += fs->block_size - within;
            continue;
        }
        if (length < 33 || *cursor + length > dir_len)
            return -1;
        const u8 *entry = dir + *cursor;
        u32 name_len = entry[32];
        if (33 + name_len > length)
            return -1;
        record->lba = iso_le32(entry + 2);
        record->size = iso_le32(entry + 10);
        record->flags = entry[25];
        record->name = entry + 33;
        record->name_len = name_len;
        u32 susp = 33 + name_len;
        if ((name_len & 1) == 0)
            susp++;
        record->susp = susp <= length ? entry + susp : NULL;
        record->susp_len = susp <= length ? length - susp : 0;
        *cursor += length;
        return 1;
    }
    return 0;
}

/* Produce the display name for a record, preferring Rock Ridge over the
 * Joliet or primary identifier. */
static void iso_record_name(struct iso9660 *fs, const struct iso_record *rec,
                            char *out, u32 capacity)
{
    out[0] = 0;
    if (rec->name_len == 1 && rec->name[0] == 0) {
        strcpy(out, ".");
        return;
    }
    if (rec->name_len == 1 && rec->name[0] == 1) {
        strcpy(out, "..");
        return;
    }
    if (fs->susp && rec->susp && rec->susp_len > fs->susp_skip &&
        iso_rock_ridge_name(rec->susp + fs->susp_skip,
                            rec->susp_len - fs->susp_skip, out, capacity))
        return;
    if (fs->joliet) {
        iso_utf16be(rec->name, rec->name_len, out, capacity);
        return;
    }
    iso_ascii_name(rec->name, rec->name_len, out, capacity);
}

/* Concatenate all extents of a directory into one buffer. */
static int iso_read_dir(struct iso9660 *fs, const struct iso_node *node,
                        u8 **buffer, u32 *size)
{
    u64 total = 0;
    for (int i = 0; i < node->extent_count; i++)
        total += node->extents[i].size;
    if (!total || total > ISO_DIR_MAX)
        return -1;
    u8 *data = kmalloc((usize)total);
    if (!data)
        return -1;
    u32 used = 0;
    for (int i = 0; i < node->extent_count; i++) {
        if (bf_read_at(&fs->vol, (u64)node->extents[i].lba *
                                  fs->block_size,
                       data + used, node->extents[i].size) != 0) {
            kfree(data);
            return -1;
        }
        used += node->extents[i].size;
    }
    *buffer = data;
    *size = used;
    return 0;
}

/* Build the root node from the selected volume descriptor. */
static void iso_root_node(struct iso9660 *fs, struct iso_node *node)
{
    memset(node, 0, sizeof(*node));
    node->is_dir = 1;
    node->extent_count = 1;
    node->extents[0].lba = fs->root_lba;
    node->extents[0].size = fs->root_size;
    node->size = fs->root_size;
    strcpy(node->name, "/");
}

/* Scan a directory for `component`.  On success the node's extents and size
 * describe all multi-extent records that belong to the file. */
static int iso_dir_lookup(struct iso9660 *fs, const struct iso_node *dir,
                          const char *component, struct iso_node *out)
{
    u8 *data = NULL;
    u32 size = 0;
    if (iso_read_dir(fs, dir, &data, &size) != 0)
        return -1;

    u32 cursor = 0;
    int found = 0;
    struct iso_record record;
    while (iso_next_record(fs, data, size, &cursor, &record) == 1) {
        char name[ISO_NAME_MAX];
        iso_record_name(fs, &record, name, sizeof(name));
        if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        if (!bf_name_equal(name, component))
            continue;

        memset(out, 0, sizeof(*out));
        strncpy(out->name, name, sizeof(out->name) - 1);
        out->is_dir = (record.flags & 0x02) != 0;
        out->extents[0].lba = record.lba;
        out->extents[0].size = record.size;
        out->extent_count = 1;
        out->size = record.size;

        u8 flags = record.flags;
        while ((flags & 0x80) && out->extent_count < ISO_EXTENTS) {
            struct iso_record next;
            if (iso_next_record(fs, data, size, &cursor, &next) != 1)
                break;
            out->extents[out->extent_count].lba = next.lba;
            out->extents[out->extent_count].size = next.size;
            out->extent_count++;
            out->size += next.size;
            flags = next.flags;
        }
        found = 1;
        break;
    }
    kfree(data);
    return found ? 0 : -1;
}

static int iso_lookup_path(struct iso9660 *fs, const char *rel,
                           struct iso_node *out)
{
    struct iso_node current;
    iso_root_node(fs, &current);
    if (!rel || !rel[0]) {
        *out = current;
        return 0;
    }

    const char *cursor = rel;
    while (*cursor) {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;
        char component[ISO_NAME_MAX];
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 < sizeof(component))
                component[length] = *cursor;
            length++;
            cursor++;
        }
        if (!length || length >= sizeof(component))
            return -1;
        component[length] = 0;
        struct iso_node next;
        if (iso_dir_lookup(fs, &current, component, &next) != 0)
            return -1;
        current = next;
        if (*cursor && !current.is_dir)
            return -1;
    }
    *out = current;
    return 0;
}

static int iso_list(void *private, const char *rel, vfs_list_cb callback,
                    void *context)
{
    struct iso9660 *fs = private;
    struct iso_node dir;
    if (iso_lookup_path(fs, rel ? rel : "", &dir) != 0 || !dir.is_dir)
        return -1;

    u8 *data = NULL;
    u32 size = 0;
    if (iso_read_dir(fs, &dir, &data, &size) != 0)
        return -1;

    u32 cursor = 0;
    struct iso_record record;
    u8 pending_flags = 0;
    char pending_name[ISO_NAME_MAX];
    pending_name[0] = 0;
    while (iso_next_record(fs, data, size, &cursor, &record) == 1) {
        char name[ISO_NAME_MAX];
        iso_record_name(fs, &record, name, sizeof(name));
        if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        /* A multi-extent file has one entry per extent; emit it once. */
        if (pending_flags & 0x80) {
            if (strcmp(name, pending_name) == 0) {
                pending_flags = record.flags;
                continue;
            }
        }
        callback(name, record.size, (record.flags & 0x02) != 0, context);
        strncpy(pending_name, name, sizeof(pending_name) - 1);
        pending_name[sizeof(pending_name) - 1] = 0;
        pending_flags = record.flags;
    }
    kfree(data);
    return 0;
}

static int iso_stat(void *private, const char *rel, int *is_dir, u64 *size)
{
    struct iso9660 *fs = private;
    struct iso_node node;
    if (iso_lookup_path(fs, rel ? rel : "", &node) != 0)
        return -1;
    if (is_dir)
        *is_dir = node.is_dir;
    if (size)
        *size = node.size;
    return 0;
}

static int iso_read(void *private, const char *rel, u64 offset, void *buffer,
                    u64 capacity, u64 *read_out)
{
    struct iso9660 *fs = private;
    struct iso_node node;
    if (iso_lookup_path(fs, rel ? rel : "", &node) != 0 || node.is_dir)
        return -1;
    if (offset >= node.size) {
        if (read_out)
            *read_out = 0;
        return 0;
    }

    u64 remaining = node.size - offset;
    if (remaining > capacity)
        remaining = capacity;
    u8 *dst = buffer;
    u64 done = 0;
    u64 position = 0;
    for (int i = 0; i < node.extent_count && done < remaining; i++) {
        u64 extent_size = node.extents[i].size;
        if (offset >= position + extent_size) {
            position += extent_size;
            continue;
        }
        u64 within = offset > position ? offset - position : 0;
        u64 chunk = extent_size - within;
        if (chunk > remaining - done)
            chunk = remaining - done;
        u64 address = (u64)node.extents[i].lba * fs->block_size + within;
        if (bf_read_at(&fs->vol, address, dst + done, (u32)chunk) != 0)
            return -1;
        done += chunk;
        position += extent_size;
    }
    if (read_out)
        *read_out = done;
    return 0;
}

static const struct vfs_fs_ops g_iso_ops = {
    .name = "iso9660",
    .list = iso_list,
    .stat = iso_stat,
    .read = iso_read,
    .write = NULL,
    .unlink = NULL,
};

/* Read a descriptor and install the root directory it describes. */
static int iso_descriptor(struct iso9660 *fs, const u8 *block, int joliet)
{
    if (block[0] != 1 && block[0] != 2)
        return -1;
    if (memcmp(block + 1, "CD001", 5) != 0)
        return -1;
    const u8 *root = block + 156;
    if (root[0] < 34)
        return -1;
    fs->root_lba = iso_le32(root + 2);
    fs->root_size = iso_le32(root + 10);
    if (!fs->root_size)
        return -1;

    u32 block_size = bf_le16(block + 128);
    if (block_size >= 512 && block_size <= 4096)
        fs->block_size = block_size;
    fs->joliet = joliet;
    (void)joliet;
    return 0;
}

static void iso_detect_susp(struct iso9660 *fs)
{
    struct iso_node root;
    iso_root_node(fs, &root);
    u8 *data = NULL;
    u32 size = 0;
    if (iso_read_dir(fs, &root, &data, &size) != 0)
        return;
    u32 cursor = 0;
    struct iso_record record;
    if (iso_next_record(fs, data, size, &cursor, &record) == 1 &&
        record.susp && record.susp_len >= 7 &&
        record.susp[0] == 'S' && record.susp[1] == 'P' &&
        record.susp[4] == 0xBE && record.susp[5] == 0xEF) {
        fs->susp = 1;
        fs->susp_skip = record.susp[6];
    }
    kfree(data);
}

int iso9660_probe(const struct blockfs_volume *volume)
{
    if (!volume)
        return -1;
    u8 *block = kmalloc(ISO_BLOCK);
    if (!block)
        return -1;
    struct blockfs_volume probe = *volume;
    probe.logical = ISO_BLOCK;
    probe.stride = volume->stride ? volume->stride : ISO_BLOCK;
    int result = -1;
    if (bf_read_at(&probe, 16ull * ISO_BLOCK, block, ISO_BLOCK) == 0 &&
        memcmp(block + 1, "CD001", 5) == 0 && block[0] == 1)
        result = 0;
    kfree(block);
    return result;
}

int iso9660_mount_as(const struct blockfs_volume *volume, const char *path,
                     const char *type)
{
    if (!volume || !path)
        return -1;
    struct iso9660 *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return -1;
    fs->vol = *volume;
    fs->vol.logical = ISO_BLOCK;
    if (!fs->vol.stride)
        fs->vol.stride = ISO_BLOCK;
    fs->block_size = ISO_BLOCK;

    u8 *block = kmalloc(ISO_BLOCK);
    if (!block) {
        kfree(fs);
        return -1;
    }

    int have_primary = 0;
    int have_joliet = 0;
    for (u32 lba = 16; lba < 24; lba++) {
        if (bf_read_at(&fs->vol, (u64)lba * fs->block_size, block,
                       fs->block_size) != 0)
            break;
        if (memcmp(block + 1, "CD001", 5) != 0)
            break;
        if (block[0] == 255)
            break;
        if (block[0] == 1 && !have_primary) {
            have_primary = iso_descriptor(fs, block, 0) == 0;
        } else if (block[0] == 2 && block[88] == 0x25 &&
                   block[89] == 0x2F) {
            /* Joliet level 1/2/3 is identified by the third escape byte. */
            if (block[90] == 0x40 || block[90] == 0x43 ||
                block[90] == 0x45)
                have_joliet = iso_descriptor(fs, block, 1) == 0;
        }
    }
    kfree(block);
    if (!have_primary && !have_joliet) {
        kfree(fs);
        return -1;
    }
    if (!fs->block_size)
        fs->block_size = ISO_BLOCK;
    iso_detect_susp(fs);
    if (vfs_mount(path, type ? type : "iso9660", &g_iso_ops, fs) != 0) {
        kfree(fs);
        return -1;
    }
    kprintf("[iso ] %s volume at %s (root LBA %u, %u bytes, %s names)\n",
            type ? type : "iso9660", path, fs->root_lba, fs->root_size,
            fs->joliet ? "Joliet" : (fs->susp ? "Rock Ridge" : "ISO"));
    return 0;
}

int iso9660_mount_volume(const struct blockfs_volume *volume, const char *path)
{
    return iso9660_mount_as(volume, path, "iso9660");
}
