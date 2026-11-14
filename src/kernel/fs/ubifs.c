/*
 * Zhaoxu OS - read-only UBIFS driver.
 *
 * UBIFS normally sits on UBI, which wear-levels logical erase blocks (LEBs)
 * across physical erase blocks (PEBs).  This driver attaches a raw UBI image
 * from a block device, selects the UBIFS volume, validates every node CRC
 * and builds a flat in-memory node index from the valid nodes in all LEBs.
 * That avoids a writable journal replay while still reading the committed
 * tree.  zlib-compressed data nodes are supported; LZO nodes are reported as
 * unsupported instead of being returned as garbage.
 */
#include <blockfs.h>
#include <crc32.h>
#include <inflate.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define UBI_EC_MAGIC 0x55424923u /* "UBI#" */
#define UBI_VID_MAGIC 0x55424921u /* "UBI!" */
#define UBI_LAYOUT_VOLUME_ID 0x7FFu

#define UBIFS_MAGIC 0x06101831u
#define UBIFS_NODE_INO 0
#define UBIFS_NODE_DATA 1
#define UBIFS_NODE_DENT 2
#define UBIFS_NODE_XENT 3
#define UBIFS_NODE_TRUN 4
#define UBIFS_NODE_SB 6
#define UBIFS_NODE_MST 7
#define UBIFS_NODE_IDX 9
#define UBIFS_NODE_MAX 13

#define UBIFS_BLOCK 4096u
#define UBIFS_ROOT_INO 1u
#define UBIFS_NAME_MAX 256
#define UBIFS_ENTRY_MAX 8192
#define UBIFS_PEB_MAX 16384

struct ubifs_leb {
    u32 peb;
    u32 data_size;
    u32 data_offset;
    u64 sqnum;
    int used;
};

struct ubifs_entry {
    u8 key[16];
    u32 lnum;
    u32 offset;
    u32 length;
    u64 sqnum;
    u8 type;
};

struct ubifs {
    struct blockfs_volume vol;
    u32 peb_size;
    u32 leb_size;
    u32 leb_cnt;
    u32 min_io;
    u32 peb_count;
    u32 volume_id;
    u8 key_hash;
    u16 default_compr;
    struct ubifs_leb *map;
    u32 map_count;
    struct ubifs_entry *entries;
    int entry_count;
    int entry_capacity;
};

static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u64 be64(const u8 *p)
{
    return ((u64)be32(p) << 32) | be32(p + 4);
}

static u32 le32p(const u8 *p) { return bf_le32(p); }
static u64 le64p(const u8 *p) { return bf_le64(p); }

/* Read bytes from a LEB, translating through the LEB-to-PEB map. */
static int ubifs_leb_read(struct ubifs *fs, u32 lnum, u32 offset, void *out,
                          u32 length)
{
    if (lnum >= fs->map_count || !fs->map[lnum].used)
        return -1;
    const struct ubifs_leb *leb = &fs->map[lnum];
    if (offset > leb->data_size || length > leb->data_size - offset)
        return -1;
    u64 address = (u64)leb->peb * fs->peb_size + leb->data_offset + offset;
    return bf_read_at(&fs->vol, address, out, length);
}

/* Validate one on-disk node header and its CRC. */
static int ubifs_load_node(struct ubifs *fs, u32 lnum, u32 offset,
                           u8 **node, u32 *node_len)
{
    u8 header[24];
    if (ubifs_leb_read(fs, lnum, offset, header, sizeof(header)) != 0)
        return -1;
    if (le32p(header) != UBIFS_MAGIC)
        return -1;
    u32 length = le32p(header + 16);
    u8 type = header[20];
    if (length < 24 || length > fs->leb_size || type > UBIFS_NODE_MAX)
        return -1;
    if (offset + length > fs->leb_size)
        return -1;

    u8 *data = kmalloc(length);
    if (!data)
        return -1;
    if (ubifs_leb_read(fs, lnum, offset, data, length) != 0) {
        kfree(data);
        return -1;
    }
    if (crc32_le(0, data + 8, length - 8) != le32p(data + 4)) {
        kfree(data);
        return -1;
    }
    *node = data;
    *node_len = length;
    return 0;
}

static u32 key_inum(const u8 *key)
{
    return le32p(key) >> 8;
}

/* Record one node in the flat index if it is a keyed object node. */
static void ubifs_index_node(struct ubifs *fs, const u8 *node, u32 length,
                             u8 type, u32 lnum, u32 offset)
{
    if (type > UBIFS_NODE_TRUN || fs->entry_count >= fs->entry_capacity)
        return;
    if (length < 40)
        return;
    struct ubifs_entry *entry = &fs->entries[fs->entry_count++];
    memcpy(entry->key, node + 24, 16);
    entry->lnum = lnum;
    entry->offset = offset;
    entry->length = length;
    entry->sqnum = le64p(node + 8);
    entry->type = type;
}

/* Walk every used byte of every LEB and index valid nodes.  Alignment
 * mismatches are skipped 8 bytes at a time so a partially overwritten LEB
 * still yields its later nodes. */
static void ubifs_scan(struct ubifs *fs)
{
    fs->entry_count = 0;
    for (u32 lnum = 0; lnum < fs->leb_cnt && lnum < fs->map_count; lnum++) {
        if (!fs->map[lnum].used)
            continue;
        u32 limit = fs->map[lnum].data_size;
        if (limit > fs->leb_size)
            limit = fs->leb_size;
        u32 offset = 0;
        while (offset + 24 <= limit) {
            u8 header[24];
            if (ubifs_leb_read(fs, lnum, offset, header, sizeof(header))) {
                offset += 8;
                continue;
            }
            if (le32p(header) != UBIFS_MAGIC) {
                offset += 8;
                continue;
            }
            u32 length = le32p(header + 16);
            u8 type = header[20];
            if (length < 24 || length > fs->leb_size - offset ||
                type > UBIFS_NODE_MAX) {
                offset += 8;
                continue;
            }
            u8 *node = NULL;
            u32 node_len = 0;
            if (ubifs_load_node(fs, lnum, offset, &node, &node_len) == 0) {
                ubifs_index_node(fs, node, node_len, type, lnum, offset);
                kfree(node);
                offset += (node_len + 7) & ~7u;
            } else {
                offset += 8;
            }
        }
    }
}

/* Find the newest index entry for an object key.  `type` and `inum` narrow
 * the search before the full key comparison. */
static int ubifs_find_entry(struct ubifs *fs, u8 type, u32 inum,
                            const u8 *key, struct ubifs_entry *out)
{
    int found = 0;
    u64 best = 0;
    for (int i = 0; i < fs->entry_count; i++) {
        struct ubifs_entry *entry = &fs->entries[i];
        if (entry->type != type || key_inum(entry->key) != inum)
            continue;
        if (key && memcmp(entry->key, key, 16) != 0)
            continue;
        if (!found || entry->sqnum > best) {
            *out = *entry;
            best = entry->sqnum;
            found = 1;
        }
    }
    return found ? 0 : -1;
}

/* Read and parse an inode node. */
static int ubifs_inode(struct ubifs *fs, u32 inum, u64 *size, u32 *mode)
{
    struct ubifs_entry entry;
    if (ubifs_find_entry(fs, UBIFS_NODE_INO, inum, NULL, &entry) != 0)
        return -1;
    u8 *node = NULL;
    u32 length = 0;
    if (ubifs_load_node(fs, entry.lnum, entry.offset, &node, &length) != 0)
        return -1;
    if (length < 120) {
        kfree(node);
        return -1;
    }
    if (size)
        *size = le64p(node + 48);
    if (mode)
        *mode = le32p(node + 104);
    kfree(node);
    return 0;
}

static u64 ubifs_xent(struct ubifs *fs, u32 parent, const char *name);

/* Resolve one directory entry inside `parent` and return its inode. */
static int ubifs_dent(struct ubifs *fs, u32 parent, const char *name,
                      u32 *inum, int *is_dir)
{
    int found = 0;
    u64 best = 0;
    u32 result_inum = 0;
    int result_dir = 0;
    u32 name_len = (u32)strlen(name);

    for (int i = 0; i < fs->entry_count; i++) {
        struct ubifs_entry *entry = &fs->entries[i];
        if (entry->type != UBIFS_NODE_DENT ||
            key_inum(entry->key) != parent)
            continue;
        if (found && entry->sqnum <= best)
            continue;
        u8 *node = NULL;
        u32 length = 0;
        if (ubifs_load_node(fs, entry->lnum, entry->offset, &node,
                            &length) != 0)
            continue;
        if (length < 56) {
            kfree(node);
            continue;
        }
        u32 dent_inum = (u32)le64p(node + 40);
        u8 dent_type = node[49];
        u16 dent_len = bf_le16(node + 50);
        if (56u + dent_len <= length && dent_len == name_len &&
            memcmp(node + 56, name, name_len) == 0) {
            result_inum = dent_inum;
            result_dir = dent_type == 1;
            best = entry->sqnum;
            found = 1;
        }
        kfree(node);
    }
    if (!found)
        return -1;
    /* A newer whiteout removes the name even though the dent node remains
     * in the journal. */
    if (ubifs_xent(fs, parent, name) > best)
        return -1;
    if (inum)
        *inum = result_inum;
    if (is_dir)
        *is_dir = result_dir;
    return 0;
}

/* Return the newest whiteout sequence number for a name, or zero. */
static u64 ubifs_xent(struct ubifs *fs, u32 parent, const char *name)
{
    u64 best = 0;
    u32 name_len = (u32)strlen(name);
    for (int i = 0; i < fs->entry_count; i++) {
        struct ubifs_entry *entry = &fs->entries[i];
        if (entry->type != UBIFS_NODE_XENT ||
            key_inum(entry->key) != parent || entry->sqnum <= best)
            continue;
        u8 *node = NULL;
        u32 length = 0;
        if (ubifs_load_node(fs, entry->lnum, entry->offset, &node,
                            &length) != 0)
            continue;
        if (length >= 56) {
            u16 dent_len = bf_le16(node + 50);
            if (56u + dent_len <= length && dent_len == name_len &&
                memcmp(node + 56, name, name_len) == 0)
                best = entry->sqnum;
        }
        kfree(node);
    }
    return best;
}

struct ubifs_seen {
    u32 count;
    char names[128][UBIFS_NAME_MAX];
};

static int ubifs_seen_add(struct ubifs_seen *seen, const char *name)
{
    for (u32 i = 0; i < seen->count; i++)
        if (strcmp(seen->names[i], name) == 0)
            return 0;
    if (seen->count >= 128)
        return 0;
    strncpy(seen->names[seen->count], name, UBIFS_NAME_MAX - 1);
    seen->names[seen->count][UBIFS_NAME_MAX - 1] = 0;
    seen->count++;
    return 1;
}

/* Resolve a path to an inode number and the size/mode of its inode. */
static int ubifs_lookup(struct ubifs *fs, const char *rel, u32 *inode,
                        u64 *size, u32 *mode)
{
    u32 current = UBIFS_ROOT_INO;
    const char *cursor = rel ? rel : "";
    while (*cursor) {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;
        char component[UBIFS_NAME_MAX];
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
        u32 next = 0;
        if (ubifs_dent(fs, current, component, &next, NULL) != 0)
            return -1;
        current = next;
    }
    if (ubifs_inode(fs, current, size, mode) != 0)
        return -1;
    if (inode)
        *inode = current;
    return 0;
}

static int ubifs_list(void *private, const char *rel, vfs_list_cb callback,
                      void *context)
{
    struct ubifs *fs = private;
    u32 inode = UBIFS_ROOT_INO;
    u64 size = 0;
    u32 mode = 0;
    if (ubifs_lookup(fs, rel ? rel : "", &inode, &size, &mode) != 0 ||
        (mode & 0170000u) != 0040000u)
        return -1;

    struct ubifs_seen *seen = kzalloc(sizeof(*seen));
    if (!seen)
        return -1;

    for (int i = 0; i < fs->entry_count; i++) {
        struct ubifs_entry *entry = &fs->entries[i];
        if (entry->type != UBIFS_NODE_DENT ||
            key_inum(entry->key) != inode)
            continue;
        u8 *node = NULL;
        u32 length = 0;
        if (ubifs_load_node(fs, entry->lnum, entry->offset, &node,
                            &length) != 0)
            continue;
        if (length < 56) {
            kfree(node);
            continue;
        }
        u16 name_len = bf_le16(node + 50);
        if (!name_len || 56u + name_len > length ||
            name_len >= UBIFS_NAME_MAX) {
            kfree(node);
            continue;
        }
        char name[UBIFS_NAME_MAX];
        memcpy(name, node + 56, name_len);
        name[name_len] = 0;
        u8 dent_type = node[49];
        int is_dir = dent_type == 1;
        if (strcmp(name, ".") && strcmp(name, "..") &&
            ubifs_seen_add(seen, name) &&
            entry->sqnum > ubifs_xent(fs, inode, name)) {
            u32 child = (u32)le64p(node + 40);
            u64 child_size = 0;
            ubifs_inode(fs, child, &child_size, NULL);
            callback(name, child_size, is_dir, context);
        }
        kfree(node);
    }
    kfree(seen);
    return 0;
}

static int ubifs_stat(void *private, const char *rel, int *is_dir, u64 *size)
{
    struct ubifs *fs = private;
    u64 file_size = 0;
    u32 mode = 0;
    if (ubifs_lookup(fs, rel ? rel : "", NULL, &file_size, &mode) != 0)
        return -1;
    if (is_dir)
        *is_dir = (mode & 0170000u) == 0040000u;
    if (size)
        *size = file_size;
    return 0;
}

/* Read one 4 KiB UBIFS block, decompressing it when needed. */
static int ubifs_block(struct ubifs *fs, u32 inum, u32 block, u8 *out,
                       u32 *out_len)
{
    struct ubifs_entry entry;
    u8 key[16];
    memset(key, 0, sizeof(key));
    u32 word = (inum << 8) | UBIFS_NODE_DATA;
    memcpy(key, &word, 4);
    memcpy(key + 4, &block, 4);
    if (ubifs_find_entry(fs, UBIFS_NODE_DATA, inum, key, &entry) != 0)
        return -1;
    u8 *node = NULL;
    u32 length = 0;
    if (ubifs_load_node(fs, entry.lnum, entry.offset, &node, &length) != 0)
        return -1;
    if (length < 48) {
        kfree(node);
        return -1;
    }
    u32 compressed_size = le32p(node + 40);
    u16 compression = bf_le16(node + 44);
    if (48u + compressed_size > length) {
        kfree(node);
        return -1;
    }
    const u8 *payload = node + 48;
    int result = -1;
    if (compression == 0) {
        if (compressed_size <= UBIFS_BLOCK) {
            memcpy(out, payload, compressed_size);
            *out_len = compressed_size;
            result = 0;
        }
    } else if (compression == 2) {
        u32 produced = 0;
        if (zlib_decompress(payload, compressed_size, out, UBIFS_BLOCK,
                            &produced) == 0) {
            *out_len = produced;
            result = 0;
        }
    }
    kfree(node);
    return result;
}

static int ubifs_read(void *private, const char *rel, u64 offset, void *buffer,
                      u64 capacity, u64 *read_out)
{
    struct ubifs *fs = private;
    u64 size = 0;
    u32 mode = 0;
    u32 inode = 0;
    if (ubifs_lookup(fs, rel ? rel : "", &inode, &size, &mode) != 0 ||
        (mode & 0170000u) == 0040000u)
        return -1;
    if (offset >= size) {
        if (read_out)
            *read_out = 0;
        return 0;
    }
    u64 count = size - offset;
    if (count > capacity)
        count = capacity;
    if (!count) {
        if (read_out)
            *read_out = 0;
        return 0;
    }

    u8 *block = kmalloc(UBIFS_BLOCK);
    if (!block)
        return -1;
    u8 *dst = buffer;
    u64 done = 0;
    while (done < count) {
        u64 position = offset + done;
        u32 index = (u32)(position / UBIFS_BLOCK);
        u32 within = (u32)(position % UBIFS_BLOCK);
        u32 block_len = 0;
        if (ubifs_block(fs, inode, index, block, &block_len) != 0) {
            /* A hole reads back as zeroes. */
            memset(block, 0, UBIFS_BLOCK);
            block_len = UBIFS_BLOCK;
        }
        if (block_len > UBIFS_BLOCK)
            block_len = UBIFS_BLOCK;

        u32 available = block_len > within ? block_len - within : 0;
        u64 want = count - done;
        if (want > UBIFS_BLOCK - within)
            want = UBIFS_BLOCK - within;
        u32 copy = (u32)want;
        if (copy > available)
            copy = available;
        if (copy)
            memcpy(dst + done, block + within, copy);
        if (copy < want)
            memset(dst + done + copy, 0, (u32)want - copy);
        done += want;
    }
    kfree(block);
    if (read_out)
        *read_out = done;
    return done ? 0 : -1;
}

static const struct vfs_fs_ops g_ubifs_ops = {
    .name = "ubifs",
    .list = ubifs_list,
    .stat = ubifs_stat,
    .read = ubifs_read,
    .write = NULL,
    .unlink = NULL,
};

/* Scan the UBI headers for the PEB size that makes sense of the image. */
static u32 ubifs_detect_peb(struct ubifs *fs, u32 *data_offset)
{
    /* Almost every non-UBI volume is rejected by this single four-byte
     * read; scanning 64 PEB candidates is only worthwhile after it hits. */
    u8 magic[4];
    if (bf_read_at(&fs->vol, 0, magic, sizeof(magic)) != 0 ||
        be32(magic) != UBI_EC_MAGIC)
        return 0;

    static const u32 candidates[] = {
        128u * 1024u, 256u * 1024u, 64u * 1024u, 512u * 1024u,
        1024u * 1024u, 2048u * 1024u, 32u * 1024u};
    u64 bytes = fs->vol.sectors * fs->vol.logical;
    u32 best_size = 0;
    u32 best_count = 0;
    u32 best_offset = 0;

    for (u32 c = 0; c < sizeof(candidates) / sizeof(candidates[0]); c++) {
        u32 peb_size = candidates[c];
        if (bytes < (u64)peb_size * 2)
            continue;
        u32 count = 0;
        u32 offset = 0;
        u32 pebs = (u32)(bytes / peb_size);
        if (pebs > 64)
            pebs = 64; /* probing only needs the first blocks */
        for (u32 i = 0; i < pebs; i++) {
            u8 header[64];
            if (bf_read_at(&fs->vol, (u64)i * peb_size, header,
                           sizeof(header)))
                continue;
            if (be32(header) != UBI_EC_MAGIC || header[4] != 1)
                continue;
            u32 vid_off = be32(header + 16);
            u32 data_off = be32(header + 20);
            if (vid_off < 64 || data_off <= vid_off || data_off >= peb_size)
                continue;
            u8 vid[64];
            if (bf_read_at(&fs->vol, (u64)i * peb_size + vid_off, vid,
                           sizeof(vid)) != 0)
                continue;
            if (be32(vid) != UBI_VID_MAGIC)
                continue;
            count++;
            offset = data_off;
        }
        if (count > best_count) {
            best_count = count;
            best_size = peb_size;
            best_offset = offset;
        }
    }
    if (!best_size)
        return 0;
    fs->peb_size = best_size;
    *data_offset = best_offset;
    return best_count;
}

/* Attach UBI: map the volume's LEBs to their newest PEB copies. */
static int ubifs_attach(struct ubifs *fs)
{
    u32 fallback_offset = 0;
    u32 valid = ubifs_detect_peb(fs, &fallback_offset);
    if (!valid)
        return -1;

    u64 bytes = fs->vol.sectors * fs->vol.logical;
    fs->peb_count = (u32)(bytes / fs->peb_size);
    if (fs->peb_count > UBIFS_PEB_MAX)
        fs->peb_count = UBIFS_PEB_MAX;

    /* First pass: discover the volume ids and the highest LEB number. */
    u32 max_lnum = 0;
    u32 candidate_ids[16];
    u32 candidate_count = 0;
    for (u32 i = 0; i < fs->peb_count; i++) {
        u8 ec[64];
        if (bf_read_at(&fs->vol, (u64)i * fs->peb_size, ec, sizeof(ec)))
            continue;
        if (be32(ec) != UBI_EC_MAGIC)
            continue;
        u32 vid_off = be32(ec + 16);
        u8 vid[64];
        if (bf_read_at(&fs->vol, (u64)i * fs->peb_size + vid_off, vid,
                       sizeof(vid)))
            continue;
        if (be32(vid) != UBI_VID_MAGIC)
            continue;
        u32 volume_id = be32(vid + 8);
        u32 lnum = be32(vid + 12);
        if (volume_id == UBI_LAYOUT_VOLUME_ID)
            continue;
        if (lnum > max_lnum)
            max_lnum = lnum;
        int known = 0;
        for (u32 k = 0; k < candidate_count; k++)
            if (candidate_ids[k] == volume_id)
                known = 1;
        if (!known && candidate_count < 16)
            candidate_ids[candidate_count++] = volume_id;
    }
    if (!candidate_count)
        return -1;

    fs->map_count = max_lnum + 1;
    fs->map = kzalloc((usize)fs->map_count * sizeof(struct ubifs_leb));
    if (!fs->map)
        return -1;

    /* Pick the volume whose LEB 0 carries a UBIFS superblock node. */
    for (u32 c = 0; c < candidate_count; c++) {
        u32 volume_id = candidate_ids[c];
        memset(fs->map, 0,
               (usize)fs->map_count * sizeof(struct ubifs_leb));
        u32 max_lnum2 = 0;
        for (u32 i = 0; i < fs->peb_count; i++) {
            u8 ec[64];
            if (bf_read_at(&fs->vol, (u64)i * fs->peb_size, ec, sizeof(ec)))
                continue;
            if (be32(ec) != UBI_EC_MAGIC)
                continue;
            u32 vid_off = be32(ec + 16);
            u32 data_off = be32(ec + 20);
            u8 vid[64];
            if (bf_read_at(&fs->vol, (u64)i * fs->peb_size + vid_off, vid,
                           sizeof(vid)))
                continue;
            if (be32(vid) != UBI_VID_MAGIC || be32(vid + 8) != volume_id)
                continue;
            u32 lnum = be32(vid + 12);
            u64 sqnum = be64(vid + 40);
            u32 data_size = be32(vid + 20);
            if (lnum >= fs->map_count)
                continue;
            if (!fs->map[lnum].used || sqnum > fs->map[lnum].sqnum) {
                fs->map[lnum].used = 1;
                fs->map[lnum].peb = i;
                fs->map[lnum].data_size = data_size;
                fs->map[lnum].data_offset = data_off;
                fs->map[lnum].sqnum = sqnum;
            }
            if (lnum > max_lnum2)
                max_lnum2 = lnum;
        }
        fs->volume_id = volume_id;

        u8 header[24];
        if (!fs->map[0].used ||
            ubifs_leb_read(fs, 0, 0, header, sizeof(header)) != 0)
            continue;
        if (le32p(header) != UBIFS_MAGIC || header[20] != UBIFS_NODE_SB)
            continue;
        return 0;
    }
    return -1;
}

/* Parse the UBIFS superblock node into geometry fields. */
static int ubifs_superblock(struct ubifs *fs)
{
    u8 *node = NULL;
    u32 length = 0;
    if (ubifs_load_node(fs, 0, 0, &node, &length) != 0)
        return -1;
    if (length < 128 || node[20] != UBIFS_NODE_SB) {
        kfree(node);
        return -1;
    }
    u32 leb_size = le32p(node + 36);
    u32 leb_cnt = le32p(node + 40);
    u32 min_io = le32p(node + 32);
    if (!leb_size || leb_size > fs->peb_size || !leb_cnt ||
        min_io > leb_size) {
        kfree(node);
        return -1;
    }
    fs->leb_size = leb_size;
    fs->leb_cnt = leb_cnt;
    fs->min_io = min_io;
    fs->key_hash = node[26];
    fs->default_compr = bf_le16(node + 84);
    kfree(node);
    return 0;
}

int ubifs_probe(const struct blockfs_volume *volume)
{
    if (!volume)
        return -1;
    struct ubifs fs;
    memset(&fs, 0, sizeof(fs));
    fs.vol = *volume;
    if (ubifs_attach(&fs) != 0)
        return -1;
    int result = ubifs_superblock(&fs);
    kfree(fs.map);
    return result;
}

int ubifs_mount_volume(const struct blockfs_volume *volume, const char *path)
{
    if (!volume || !path)
        return -1;
    struct ubifs *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return -1;
    fs->vol = *volume;
    if (ubifs_attach(fs) != 0 || ubifs_superblock(fs) != 0) {
        kfree(fs->map);
        kfree(fs);
        return -1;
    }
    fs->entry_capacity = UBIFS_ENTRY_MAX;
    fs->entries = kmalloc((usize)fs->entry_capacity *
                          sizeof(struct ubifs_entry));
    if (!fs->entries) {
        kfree(fs->map);
        kfree(fs);
        return -1;
    }
    ubifs_scan(fs);
    if (vfs_mount(path, "ubifs", &g_ubifs_ops, fs) != 0) {
        kfree(fs->entries);
        kfree(fs->map);
        kfree(fs);
        return -1;
    }
    kprintf("[ubifs] volume %u on disk %d LBA %llu mounted at %s "
            "(%u LEBs, %u bytes per LEB, %d nodes)\n",
            fs->volume_id, fs->vol.disk, fs->vol.base, path, fs->leb_cnt,
            fs->leb_size, fs->entry_count);
    return 0;
}
