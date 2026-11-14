/*
 * Zhaoxu OS - read-only NTFS driver.
 *
 * NTFS stores metadata in MFT records made of typed attributes.  This driver
 * implements the pieces a reader needs: update-sequence fixups, resident and
 * non-resident attributes, runlists (including sparse runs), $FILE_NAME
 * names, and the $INDEX_ROOT/$INDEX_ALLOCATION B+ tree used by directories.
 * Compressed and encrypted attributes are rejected instead of misread.
 */
#include <blockfs.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define NTFS_NAME_MAX 256
#define NTFS_MAX_RUNS 64
#define NTFS_MAX_RECORD (64u * 1024u)
#define NTFS_MAX_INDEX (256u * 1024u)
#define NTFS_MAX_DEPTH 8

struct ntfs_run {
    u64 vcn;
    u64 lcn;
    u64 count;
    int sparse;
};

struct ntfs_attr {
    u32 type;
    int nonresident;
    const u8 *value;
    u32 value_len;
    u64 data_size;
    u64 alloc_size;
    u64 start_vcn;
    u32 flags;
    struct ntfs_run runs[NTFS_MAX_RUNS];
    int run_count;
};

struct ntfs {
    struct blockfs_volume vol;
    u32 bps;
    u32 spc;
    u32 cluster_size;
    u64 mft_lcn;
    u32 record_size;
    u32 index_size;
    int mounted;
};

struct ntfs_file {
    char name[NTFS_NAME_MAX];
    u64 size;
    u64 alloc_size;
    int is_dir;
    int has_data;
    u32 resident_size;
    u8 resident_data[1024];
    struct ntfs_run runs[NTFS_MAX_RUNS];
    int run_count;
};

static u16 le16(const u8 *p) { return bf_le16(p); }
static u32 le32(const u8 *p) { return bf_le32(p); }
static u64 le64(const u8 *p) { return bf_le64(p); }

/* Decode a signed little-endian integer stored in `bytes` bytes. */
static i64 signed_le(const u8 *p, u32 bytes)
{
    if (!bytes || bytes > 8)
        return 0;
    i64 value = 0;
    for (u32 i = 0; i < bytes; i++)
        value |= (i64)p[i] << (i * 8);
    if (bytes < 8) {
        u64 sign = 1ull << (bytes * 8 - 1);
        if ((u64)value & sign)
            value |= ~((i64)(sign * 2 - 1));
    }
    return value;
}

/* Parse the data-run list of a non-resident attribute.  Offsets are signed
 * deltas from the previous run; a zero offset width marks a sparse run. */
static int ntfs_runs(struct ntfs_attr *attr, const u8 *data, u32 length,
                     u32 run_offset)
{
    u32 offset = run_offset;
    u64 previous = 0;
    u64 vcn = attr->start_vcn;
    attr->run_count = 0;

    while (offset < length && attr->run_count < NTFS_MAX_RUNS) {
        u8 header = data[offset++];
        if (!header)
            break;
        u32 length_bytes = header & 0x0F;
        u32 offset_bytes = header >> 4;
        if (!length_bytes || length_bytes > 8 || offset_bytes > 8 ||
            offset + length_bytes + offset_bytes > length)
            return -1;
        u64 count = 0;
        for (u32 i = 0; i < length_bytes; i++)
            count |= (u64)data[offset + i] << (i * 8);
        offset += length_bytes;
        if (!count)
            return -1;

        i64 delta = 0;
        if (offset_bytes) {
            delta = signed_le(data + offset, offset_bytes);
            offset += offset_bytes;
        }
        i64 lcn = (i64)previous + delta;
        if (offset_bytes && lcn < 0)
            return -1;

        struct ntfs_run *run = &attr->runs[attr->run_count++];
        run->vcn = vcn;
        run->lcn = offset_bytes ? (u64)lcn : 0;
        run->count = count;
        run->sparse = offset_bytes == 0;
        if (offset_bytes)
            previous = (u64)lcn;
        vcn += count;
    }
    return attr->run_count ? 0 : -1;
}

/* Read one attribute from an MFT record starting at *offset. */
static int ntfs_attr_next(const u8 *record, u32 record_size, u32 *offset,
                          struct ntfs_attr *attr)
{
    memset(attr, 0, sizeof(*attr));
    if (*offset + 24 > record_size)
        return 0;
    const u8 *entry = record + *offset;
    attr->type = le32(entry);
    if (attr->type == 0xFFFFFFFFu)
        return 0;
    u32 length = le32(entry + 4);
    if (length < 24 || *offset + length > record_size)
        return -1;
    attr->nonresident = entry[8] != 0;
    attr->flags = le16(entry + 12);
    u8 name_len = entry[9];

    if (!attr->nonresident) {
        attr->value_len = le32(entry + 16);
        u32 value_off = le16(entry + 20);
        if (value_off < 24 || value_off > length ||
            attr->value_len > length - value_off)
            return -1;
        attr->value = entry + value_off;
        attr->data_size = attr->value_len;
        attr->alloc_size = attr->value_len;
        /* Named attributes are not needed by this reader. */
        if (name_len)
            attr->type |= 0x80000000u;
    } else {
        attr->start_vcn = le64(entry + 16);
        attr->alloc_size = le64(entry + 40);
        attr->data_size = le64(entry + 48);
        u32 run_off = le16(entry + 32);
        if (run_off < 24 || run_off >= length)
            return -1;
        if (attr->flags & 0x4001u)
            return -1; /* compressed or encrypted */
        if (ntfs_runs(attr, entry, length, run_off) != 0)
            return -1;
        if (name_len)
            attr->type |= 0x80000000u;
    }
    *offset += length;
    return 1;
}

/* Apply the update-sequence fixup that protects each record sector. */
static int ntfs_fixup(u8 *record, u32 size, u32 bps)
{
    if (size < 48 || memcmp(record, "FILE", 4) != 0)
        return -1;
    u32 usa_offset = le16(record + 4);
    u32 usa_count = le16(record + 6);
    u32 sectors = size / bps;
    if (usa_count != sectors + 1 || usa_offset + usa_count * 2 > size)
        return -1;
    const u8 *usn = record + usa_offset;
    for (u32 i = 1; i < usa_count; i++) {
        u8 *tail = record + i * bps - 2;
        if (tail[0] != usn[0] || tail[1] != usn[1])
            return -1;
        tail[0] = record[usa_offset + i * 2];
        tail[1] = record[usa_offset + i * 2 + 1];
    }
    return 0;
}

static int ntfs_read_record(struct ntfs *fs, u64 index, u8 *buffer)
{
    u64 offset = fs->mft_lcn * fs->cluster_size + index * fs->record_size;
    if (bf_read_at(&fs->vol, offset, buffer, fs->record_size) != 0)
        return -1;
    if (ntfs_fixup(buffer, fs->record_size, fs->bps) != 0)
        return -1;
    return 0;
}

/* Read a byte range from a runlist stream.  Holes read as zero. */
static int ntfs_read_runs(struct ntfs *fs, const struct ntfs_run *runs,
                          int run_count, u64 offset, u8 *dst, u64 length,
                          u64 *read_out)
{
    u64 done = 0;
    if (read_out)
        *read_out = 0;
    while (done < length) {
        u64 vcn = offset / fs->cluster_size;
        u32 within = (u32)(offset % fs->cluster_size);
        int found = 0;
        for (int i = 0; i < run_count; i++) {
            if (vcn < runs[i].vcn || vcn >= runs[i].vcn + runs[i].count)
                continue;
            u64 chunk = runs[i].count * fs->cluster_size -
                        (u64)(vcn - runs[i].vcn) * fs->cluster_size -
                        within;
            if (chunk > length - done)
                chunk = length - done;
            if (runs[i].sparse) {
                memset(dst + done, 0, (usize)chunk);
            } else {
                u64 lcn = runs[i].lcn +
                          (vcn - runs[i].vcn);
                u64 address = lcn * fs->cluster_size + within;
                if (bf_read_at(&fs->vol, address, dst + done,
                               (u32)chunk) != 0)
                    return -1;
            }
            done += chunk;
            offset += chunk;
            found = 1;
            break;
        }
        if (!found)
            return -1;
    }
    if (read_out)
        *read_out = done;
    return 0;
}

static u64 ntfs_lookup_name(const u8 *record, u32 record_size,
                            char *name, u32 capacity, u64 *parent,
                            int *is_dir, u64 *size, u64 *alloc_size)
{
    u32 offset = 0x14;
    struct ntfs_attr attr;
    int have_long = 0;
    u64 result = 0;
    while (ntfs_attr_next(record, record_size, &offset, &attr) == 1) {
        if (attr.type != 0x30 || !attr.value || attr.value_len < 66)
            continue;
        const u8 *value = attr.value;
        u32 name_length = value[64];
        u8 namespace = value[65];
        if (name_length > 255 || 66 + name_length * 2 > attr.value_len)
            continue;
        if (namespace == 2 && have_long)
            continue;
        if (namespace != 2)
            have_long = 1;
        if (parent)
            *parent = le64(value);
        if (is_dir)
            *is_dir = (le32(value + 56) & 0x10000000u) != 0;
        if (size)
            *size = le64(value + 48);
        if (alloc_size)
            *alloc_size = le64(value + 40);
        if (name && capacity) {
            u16 units[256];
            for (u32 i = 0; i < name_length; i++)
                units[i] = le16(value + 66 + i * 2);
            bf_utf16_to_utf8(units, name_length, name, capacity);
        }
        result = 1;
    }
    return result;
}

/* Pull the unnamed $DATA stream out of a record. */
static int ntfs_parse_data(const u8 *record, u32 record_size,
                           struct ntfs_file *file)
{
    u32 offset = 0x14;
    struct ntfs_attr attr;
    while (ntfs_attr_next(record, record_size, &offset, &attr) == 1) {
        if (attr.type == 0x90)
            file->is_dir = 1;
        if (attr.type != 0x80)
            continue;
        if (!attr.nonresident) {
            /* A resident stream, including a zero-length one, has no runs;
             * its bytes live inside the MFT record, so copy them out before
             * the caller releases the record buffer. */
            if (attr.value_len > sizeof(file->resident_data))
                return -1;
            file->size = attr.value_len;
            file->alloc_size = attr.value_len;
            file->resident_size = attr.value_len;
            if (attr.value_len)
                memcpy(file->resident_data, attr.value, attr.value_len);
            file->has_data = 1;
            file->run_count = 0;
            continue;
        }
        file->size = attr.data_size;
        file->alloc_size = attr.alloc_size;
        file->run_count = attr.run_count;
        if (attr.run_count > NTFS_MAX_RUNS)
            file->run_count = NTFS_MAX_RUNS;
        memcpy(file->runs, attr.runs,
               (usize)file->run_count * sizeof(struct ntfs_run));
        file->has_data = 1;
    }
    return file->has_data || file->is_dir ? 0 : -1;
}

struct ntfs_entry {
    u64 ref;
    char name[NTFS_NAME_MAX];
    int is_dir;
};

typedef int (*ntfs_entry_cb)(const struct ntfs_entry *entry, void *context);

/* Decode a FILE_NAME key inside an index entry. */
static int ntfs_index_key(const u8 *key, u32 key_len,
                          struct ntfs_entry *entry, u64 ref)
{
    if (key_len < 66)
        return -1;
    u32 name_length = key[64];
    u8 namespace = key[65];
    if (!name_length || name_length > 255 ||
        66 + name_length * 2 > key_len)
        return -1;
    if (namespace == 2)
        return -1;
    u16 units[256];
    for (u32 i = 0; i < name_length; i++)
        units[i] = le16(key + 66 + i * 2);
    bf_utf16_to_utf8(units, name_length, entry->name,
                     sizeof(entry->name));
    entry->is_dir = (le32(key + 56) & 0x10000000u) != 0;
    entry->ref = ref & 0x0000FFFFFFFFFFFFull;
    return entry->name[0] ? 0 : -1;
}

/* Read an INDX block at the given index VCN. */
static int ntfs_index_block(struct ntfs *fs, const struct ntfs_attr *alloc,
                            u64 vcn, u8 **buffer, u32 *size)
{
    u64 offset = vcn * fs->index_size;
    u8 *data = kmalloc(fs->index_size);
    if (!data)
        return -1;
    if (ntfs_read_runs(fs, alloc->runs, alloc->run_count, offset, data,
                       fs->index_size, NULL) != 0 ||
        memcmp(data, "INDX", 4) != 0 ||
        ntfs_fixup(data, fs->index_size, fs->bps) != 0) {
        kfree(data);
        return -1;
    }
    *buffer = data;
    *size = fs->index_size;
    return 0;
}

struct ntfs_scan {
    ntfs_entry_cb callback;
    void *context;
    u64 visited[64];
    int visited_count;
    int found;
    int depth;
};

/* Walk the entries of one index node and descend into child blocks.  The
 * traversal is depth-limited and remembers visited VCNs so a corrupt tree
 * cannot loop forever. */
static int ntfs_walk_index(struct ntfs *fs, const u8 *entries,
                           u32 entries_len, const struct ntfs_attr *alloc,
                           struct ntfs_scan *scan)
{
    u32 offset = 0;
    while (offset + 16 <= entries_len) {
        const u8 *entry = entries + offset;
        u32 length = le16(entry + 8);
        u32 key_len = le16(entry + 10);
        u16 flags = le16(entry + 12);
        if (length < 16 || offset + length > entries_len)
            break;
        if (flags & 0x0002)
            break;
        if (!(flags & 0x0001)) {
            struct ntfs_entry item;
            if (ntfs_index_key(entry + 16, key_len, &item, le64(entry)) == 0)
                if (scan->callback(&item, scan->context))
                    scan->found = 1;
        }
        if ((flags & 0x0001) && alloc && scan->depth < NTFS_MAX_DEPTH) {
            u64 child = le64(entry + length - 8);
            int seen = 0;
            for (int i = 0; i < scan->visited_count; i++)
                if (scan->visited[i] == child)
                    seen = 1;
            if (!seen && scan->visited_count < 64) {
                scan->visited[scan->visited_count++] = child;
                u8 *block = NULL;
                u32 size = 0;
                if (ntfs_index_block(fs, alloc, child, &block, &size) == 0) {
                    if (size >= 24) {
                        u32 header_off = le32(block + 24);
                        u32 start = 24 + header_off;
                        if (start < size) {
                            scan->depth++;
                            ntfs_walk_index(fs, block + start, size - start,
                                            alloc, scan);
                            scan->depth--;
                        }
                    }
                    kfree(block);
                }
            }
        }
        offset += length;
    }
    return scan->found ? 1 : 0;
}

/* Enumerate a directory record: the resident index root plus every index
 * allocation block it links to. */
static int ntfs_directory(struct ntfs *fs, const u8 *record, u32 record_size,
                          ntfs_entry_cb callback, void *context)
{
    u32 offset = 0x14;
    struct ntfs_attr attr;
    const u8 *root = NULL;
    u32 root_len = 0;
    struct ntfs_attr alloc;
    int have_alloc = 0;

    while (ntfs_attr_next(record, record_size, &offset, &attr) == 1) {
        if (attr.type == 0x90 && !attr.nonresident) {
            root = attr.value;
            root_len = attr.value_len;
        } else if (attr.type == 0xA0 && attr.nonresident) {
            alloc = attr;
            have_alloc = 1;
        }
    }
    if (!root || root_len < 16)
        return -1;

    struct ntfs_scan scan;
    memset(&scan, 0, sizeof(scan));
    scan.callback = callback;
    scan.context = context;

    u32 entries_off = le32(root);
    if (16 + entries_off < root_len) {
        ntfs_walk_index(fs, root + 16 + entries_off,
                        root_len - 16 - entries_off,
                        have_alloc ? &alloc : NULL, &scan);
    }
    return 0;
}

struct ntfs_find_ctx {
    const char *component;
    struct ntfs_entry match;
    int found;
};

static int ntfs_find_cb(const struct ntfs_entry *entry, void *context)
{
    struct ntfs_find_ctx *ctx = context;
    if (!bf_name_equal(entry->name, ctx->component))
        return 0;
    ctx->match = *entry;
    ctx->found = 1;
    return 1;
}

/* Resolve an absolute path to a file record.  On return `buffer` holds the
 * final MFT record and `file` its data stream description. */
static int ntfs_lookup(struct ntfs *fs, const char *path, u8 *buffer,
                       struct ntfs_file *file)
{
    if (!path || path[0] != '/')
        return -1;

    u64 ref = 5; /* $Root */
    const char *cursor = path + 1;
    for (;;) {
        if (ntfs_read_record(fs, ref, buffer) != 0)
            return -1;
        if (!*cursor) {
            memset(file, 0, sizeof(*file));
            file->is_dir = 1;
            ntfs_parse_data(buffer, fs->record_size, file);
            if (ntfs_lookup_name(buffer, fs->record_size, file->name,
                                 sizeof(file->name), NULL, &file->is_dir,
                                 NULL, NULL) == 0)
                strcpy(file->name, "/");
            return 0;
        }

        char component[NTFS_NAME_MAX];
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

        int is_dir = 0;
        ntfs_lookup_name(buffer, fs->record_size, NULL, 0, NULL, &is_dir,
                         NULL, NULL);
        if (!is_dir)
            return -1;

        struct ntfs_find_ctx ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.component = component;
        if (ntfs_directory(fs, buffer, fs->record_size, ntfs_find_cb,
                           &ctx) != 0 || !ctx.found)
            return -1;
        ref = ctx.match.ref;
        if (!*cursor) {
            if (ntfs_read_record(fs, ref, buffer) != 0)
                return -1;
            memset(file, 0, sizeof(*file));
            ntfs_lookup_name(buffer, fs->record_size, file->name,
                             sizeof(file->name), NULL, &file->is_dir, NULL,
                             NULL);
            if (ntfs_parse_data(buffer, fs->record_size, file) != 0)
                return -1;
            return 0;
        }
    }
}

static int ntfs_list_cb(const struct ntfs_entry *entry, void *context)
{
    struct {
        vfs_list_cb callback;
        void *context;
        u64 refs[128];
        int count;
    } *state = context;

    for (int i = 0; i < state->count; i++)
        if (state->refs[i] == entry->ref)
            return 0;
    if (state->count < 128)
        state->refs[state->count++] = entry->ref;
    state->callback(entry->name, 0, entry->is_dir, state->context);
    return 0;
}

static int ntfs_list(void *private, const char *rel, vfs_list_cb callback,
                     void *context)
{
    struct ntfs *fs = private;
    char path[VFS_PATH_MAX];
    usize length = strlen(rel ? rel : "");
    if (length + 2 > sizeof(path))
        return -1;
    path[0] = '/';
    if (length)
        memcpy(path + 1, rel, length + 1);
    else
        path[1] = 0;

    u8 *record = kmalloc(fs->record_size);
    if (!record)
        return -1;
    struct ntfs_file file;
    if (ntfs_lookup(fs, path, record, &file) != 0 || !file.is_dir) {
        kfree(record);
        return -1;
    }
    struct {
        vfs_list_cb callback;
        void *context;
        u64 refs[128];
        int count;
    } state;
    memset(&state, 0, sizeof(state));
    state.callback = callback;
    state.context = context;
    int result = ntfs_directory(fs, record, fs->record_size, ntfs_list_cb,
                                &state);
    kfree(record);
    return result;
}

static int ntfs_stat(void *private, const char *rel, int *is_dir, u64 *size)
{
    struct ntfs *fs = private;
    char path[VFS_PATH_MAX];
    usize length = strlen(rel ? rel : "");
    if (length + 2 > sizeof(path))
        return -1;
    path[0] = '/';
    if (length)
        memcpy(path + 1, rel, length + 1);
    else
        path[1] = 0;

    u8 *record = kmalloc(fs->record_size);
    if (!record)
        return -1;
    struct ntfs_file file;
    int result = ntfs_lookup(fs, path, record, &file);
    kfree(record);
    if (result != 0)
        return -1;
    if (is_dir)
        *is_dir = file.is_dir;
    if (size)
        *size = file.size;
    return 0;
}

static int ntfs_read(void *private, const char *rel, u64 offset, void *buffer,
                     u64 capacity, u64 *read_out)
{
    struct ntfs *fs = private;
    char path[VFS_PATH_MAX];
    usize length = strlen(rel ? rel : "");
    if (length + 2 > sizeof(path))
        return -1;
    path[0] = '/';
    if (length)
        memcpy(path + 1, rel, length + 1);
    else
        path[1] = 0;

    u8 *record = kmalloc(fs->record_size);
    if (!record)
        return -1;
    struct ntfs_file file;
    int result = ntfs_lookup(fs, path, record, &file);
    kfree(record);
    if (result != 0 || file.is_dir)
        return -1;
    if (offset >= file.size) {
        if (read_out)
            *read_out = 0;
        return 0;
    }
    u64 count = file.size - offset;
    if (count > capacity)
        count = capacity;
    if (!count) {
        if (read_out)
            *read_out = 0;
        return 0;
    }
    if (file.resident_size) {
        u64 available = file.resident_size > offset
                            ? file.resident_size - offset : 0;
        u64 copy = count < available ? count : available;
        if (copy)
            memcpy(buffer, file.resident_data + offset, (usize)copy);
        if (copy < count)
            memset((u8 *)buffer + copy, 0, (usize)(count - copy));
        if (read_out)
            *read_out = count;
        return 0;
    }
    if (!file.run_count)
        return 0; /* resident empty stream */
    return ntfs_read_runs(fs, file.runs, file.run_count, offset, buffer,
                          count, read_out);
}

static const struct vfs_fs_ops g_ntfs_ops = {
    .name = "ntfs",
    .list = ntfs_list,
    .stat = ntfs_stat,
    .read = ntfs_read,
    .write = NULL,
    .unlink = NULL,
};

static int ntfs_parse_boot(const u8 *boot, struct ntfs *fs)
{
    if (memcmp(boot + 3, "NTFS    ", 8) != 0)
        return -1;
    u32 bps = le16(boot + 11);
    u32 spc = boot[13];
    u64 total = le64(boot + 40);
    u64 mft = le64(boot + 48);
    i8 cpr = (i8)boot[64];
    i8 cpi = (i8)boot[68];
    if (bps < 512 || bps > 4096 || (bps & (bps - 1)))
        return -1;
    if (!spc || (spc & (spc - 1)) || !total || !mft)
        return -1;
    u32 cluster = bps * spc;
    u32 record = cpr > 0 ? (u32)cpr * cluster : 1u << (-cpr);
    u32 index = cpi > 0 ? (u32)cpi * cluster : 1u << (-cpi);
    if (!record || record > NTFS_MAX_RECORD || record % bps)
        return -1;
    if (!index || index > NTFS_MAX_INDEX || index % bps)
        return -1;
    if (mft * cluster >= total * bps)
        return -1;

    fs->bps = bps;
    fs->spc = spc;
    fs->cluster_size = cluster;
    fs->mft_lcn = mft;
    fs->record_size = record;
    fs->index_size = index;
    return 0;
}

int ntfs_probe(const struct blockfs_volume *volume)
{
    if (!volume)
        return -1;
    u8 boot[512];
    if (bf_read_at(volume, 0, boot, sizeof(boot)) != 0)
        return -1;
    struct ntfs fs;
    memset(&fs, 0, sizeof(fs));
    fs.vol = *volume;
    return ntfs_parse_boot(boot, &fs) == 0 ? 0 : -1;
}

int ntfs_mount_volume(const struct blockfs_volume *volume, const char *path)
{
    if (!volume || !path)
        return -1;
    u8 boot[512];
    if (bf_read_at(volume, 0, boot, sizeof(boot)) != 0)
        return -1;
    struct ntfs *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return -1;
    fs->vol = *volume;
    if (ntfs_parse_boot(boot, fs) != 0) {
        kfree(fs);
        return -1;
    }
    fs->mounted = 1;
    if (vfs_mount(path, "ntfs", &g_ntfs_ops, fs) != 0) {
        kfree(fs);
        return -1;
    }
    kprintf("[ntfs] disk %d LBA %llu mounted at %s (%u-byte clusters, "
            "%u-byte MFT records)\n", fs->vol.disk, fs->vol.base, path,
            fs->cluster_size, fs->record_size);
    return 0;
}
