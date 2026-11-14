/*
 * Zhaoxu OS - read-only UDF 1.02/2.00/2.01/2.60 driver.
 *
 * UDF is a descriptor-tree filesystem: the anchor points at a volume
 * descriptor sequence which yields the partition layout, the logical volume
 * and the file set descriptor.  Files and directories are ICBs (file
 * entries) whose allocation descriptors are short_ad or long_ad records.
 * This reader supports recorded, sparse and in-ICB allocations.
 */
#include <blockfs.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define UDF_BLOCK 2048u
#define UDF_NAME_MAX 256
#define UDF_DIR_MAX (4u * 1024u * 1024u)
#define UDF_MAX_EXTENTS 32
#define UDF_MAX_PARTS 8
#define UDF_MAX_MAPS 8

#define UDF_TAG_AVDP 2
#define UDF_TAG_PD 5
#define UDF_TAG_LVD 6
#define UDF_TAG_TD 8
#define UDF_TAG_FSD 256
#define UDF_TAG_FID 257
#define UDF_TAG_FE 261

struct udf_extent {
    u64 offset;
    u64 length;
    int sparse;
};

struct udf_file {
    int is_dir;
    u64 size;
    int extent_count;
    struct udf_extent extents[UDF_MAX_EXTENTS];
    int in_icb_size;
    u8 in_icb[UDF_BLOCK];
};

struct udf_part {
    u16 number;
    u32 start;
};

struct udf_fs {
    struct blockfs_volume vol;
    u32 block_size;
    struct udf_part parts[UDF_MAX_PARTS];
    int part_count;
    u16 maps[UDF_MAX_MAPS];
    int map_count;
    u32 root_block;
    u16 root_part;
    u32 root_size;
};

static u16 le16(const u8 *p) { return bf_le16(p); }
static u32 le32(const u8 *p) { return bf_le32(p); }
static u64 le64(const u8 *p) { return bf_le64(p); }

/* Translate a partition-relative logical block into a byte offset. */
static u64 udf_address(const struct udf_fs *fs, u16 part, u32 block)
{
    u32 start = 0;
    for (int i = 0; i < fs->part_count; i++)
        if (fs->parts[i].number == part) {
            start = fs->parts[i].start;
            break;
        }
    return ((u64)start + block) * fs->block_size;
}

static int udf_read_block(struct udf_fs *fs, u64 byte_offset, u8 *out)
{
    return bf_read_at(&fs->vol, byte_offset, out, fs->block_size);
}

/* Decode an OSTA compressed Unicode name (compression id 8 or 16). */
static void udf_name(const u8 *data, u32 length, char *out, u32 capacity)
{
    if (!length) {
        out[0] = 0;
        return;
    }
    u8 compression = data[0];
    u32 bytes = length - 1;
    if (compression == 16) {
        u16 units[UDF_NAME_MAX];
        u32 count = bytes / 2;
        if (count >= UDF_NAME_MAX)
            count = UDF_NAME_MAX - 1;
        for (u32 i = 0; i < count; i++)
            units[i] = (u16)((data[1 + i * 2] << 8) | data[2 + i * 2]);
        bf_utf16_to_utf8(units, count, out, capacity);
        return;
    }
    /* Compression 8 is a byte string; bytes above ASCII are Latin-1. */
    u32 used = 0;
    for (u32 i = 0; i < bytes && used + 2 < capacity; i++) {
        u8 c = data[1 + i];
        if (c < 0x80) {
            out[used++] = (char)c;
        } else {
            out[used++] = (char)(0xC0 | (c >> 6));
            out[used++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[used] = 0;
}

/* Parse allocation descriptors into byte extents. */
static int udf_ads(struct udf_fs *fs, const u8 *ads, u32 length, int long_ad,
                   struct udf_file *file)
{
    u32 offset = 0;
    file->extent_count = 0;
    while (offset + (long_ad ? 16u : 8u) <= length &&
           file->extent_count < UDF_MAX_EXTENTS) {
        const u8 *ad = ads + offset;
        u32 raw = le32(ad);
        u32 type = raw >> 30;
        u32 bytes = raw & 0x3FFFFFFFu;
        u32 block;
        u16 part = 0;
        if (long_ad) {
            block = le32(ad + 4);
            part = le16(ad + 8);
        } else {
            block = le32(ad + 4);
            part = fs->part_count ? fs->parts[0].number : 0;
        }
        if (type == 3)
            break; /* allocation-extent continuation is not generated */
        if (!bytes)
            break;
        struct udf_extent *extent = &file->extents[file->extent_count++];
        extent->offset = udf_address(fs, part, block);
        extent->length = bytes;
        extent->sparse = type != 0;
        offset += long_ad ? 16 : 8;
    }
    return 0;
}

/* Load a file entry (ICB).  `buffer` must be one block. */
static int udf_file_entry(struct udf_fs *fs, u32 block, u16 part,
                          struct udf_file *file, u8 *buffer)
{
    if (udf_read_block(fs, udf_address(fs, part, block), buffer) != 0)
        return -1;
    if (le16(buffer) != UDF_TAG_FE)
        return -1;

    memset(file, 0, sizeof(*file));
    u8 file_type = buffer[27];
    file->is_dir = file_type == 4;
    file->size = le64(buffer + 56);
    u16 flags = le16(buffer + 34);
    u32 ad_type = flags & 0x0007u;
    u32 length_ea = le32(buffer + 168);
    u32 length_ad = le32(buffer + 172);
    u32 ad_off = 176 + length_ea;
    if (ad_off > fs->block_size || length_ad > fs->block_size - ad_off)
        return -1;

    if (ad_type == 3) {
        /* Data embedded directly in the ICB. */
        u32 size = (u32)file->size;
        if (size > fs->block_size - ad_off)
            size = fs->block_size - ad_off;
        if (size > sizeof(file->in_icb))
            size = sizeof(file->in_icb);
        memcpy(file->in_icb, buffer + ad_off, size);
        file->in_icb_size = (int)size;
        return 0;
    }
    return udf_ads(fs, buffer + ad_off, length_ad, ad_type == 1, file);
}

/* Read a file's contents into one heap buffer. */
static int udf_load(struct udf_fs *fs, const struct udf_file *file,
                    u8 **out, u32 *size)
{
    if (file->size > UDF_DIR_MAX)
        return -1;
    u32 total = (u32)file->size;
    if (!total) {
        u8 *empty = kmalloc(1);
        if (!empty)
            return -1;
        *out = empty;
        *size = 0;
        return 0;
    }
    u8 *data = kmalloc(total);
    if (!data)
        return -1;
    if (file->in_icb_size) {
        u32 copy = (u32)file->in_icb_size < total
                       ? (u32)file->in_icb_size : total;
        memcpy(data, file->in_icb, copy);
        if (copy < total)
            memset(data + copy, 0, total - copy);
        *out = data;
        *size = total;
        return 0;
    }

    u32 used = 0;
    for (int i = 0; i < file->extent_count && used < total; i++) {
        u64 length = file->extents[i].length;
        if (length > total - used)
            length = total - used;
        if (file->extents[i].sparse) {
            memset(data + used, 0, (usize)length);
        } else if (bf_read_at(&fs->vol, file->extents[i].offset,
                              data + used, (u32)length) != 0) {
            kfree(data);
            return -1;
        }
        used += (u32)length;
    }
    if (used < total)
        memset(data + used, 0, total - used);
    *out = data;
    *size = total;
    return 0;
}

struct udf_fid {
    char name[UDF_NAME_MAX];
    u32 block;
    u16 part;
    int is_dir;
    int is_parent;
};

/* Parse one file identifier descriptor. */
static int udf_fid(const u8 *data, u32 available, struct udf_fid *fid)
{
    if (available < 38 || le16(data) != UDF_TAG_FID)
        return -1;
    u8 characteristics = data[18];
    u32 name_len = data[19];
    u32 impl_len = data[36];
    if (38u + impl_len + name_len > available)
        return -1;
    u32 total = 38 + impl_len + name_len;
    total = (total + 3) & ~3u;

    memset(fid, 0, sizeof(*fid));
    fid->is_dir = (characteristics & 0x02) != 0;
    fid->is_parent = (characteristics & 0x08) != 0;
    fid->block = le32(data + 24);
    fid->part = le16(data + 28);
    udf_name(data + 38 + impl_len, name_len, fid->name, sizeof(fid->name));
    return (int)total;
}

/* Resolve a path to a file entry. */
static int udf_lookup(struct udf_fs *fs, const char *rel,
                      struct udf_file *file)
{
    u8 *block = kmalloc(fs->block_size);
    if (!block)
        return -1;

    u32 current_block = fs->root_block;
    u16 current_part = fs->root_part;
    if (!rel || !rel[0]) {
        int result = udf_file_entry(fs, current_block, current_part, file,
                                    block);
        kfree(block);
        return result;
    }

    const char *cursor = rel;
    while (*cursor) {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;
        char component[UDF_NAME_MAX];
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 < sizeof(component))
                component[length] = *cursor;
            length++;
            cursor++;
        }
        if (!length || length >= sizeof(component)) {
            kfree(block);
            return -1;
        }
        component[length] = 0;

        struct udf_file dir;
        if (udf_file_entry(fs, current_block, current_part, &dir, block) ||
            !dir.is_dir) {
            kfree(block);
            return -1;
        }
        u8 *data = NULL;
        u32 size = 0;
        if (udf_load(fs, &dir, &data, &size) != 0) {
            kfree(block);
            return -1;
        }

        int matched = 0;
        struct udf_fid fid;
        u32 offset = 0;
        while (offset + 38 <= size) {
            int step = udf_fid(data + offset, size - offset, &fid);
            if (step <= 0)
                break;
            if (!fid.is_parent && bf_name_equal(fid.name, component)) {
                matched = 1;
                break;
            }
            offset += (u32)step;
        }
        kfree(data);
        if (!matched) {
            kfree(block);
            return -1;
        }
        current_block = fid.block;
        current_part = fid.part;
    }

    int result = udf_file_entry(fs, current_block, current_part, file,
                                block);
    kfree(block);
    return result;
}

static int udf_list(void *private, const char *rel, vfs_list_cb callback,
                    void *context)
{
    struct udf_fs *fs = private;
    struct udf_file dir;
    if (udf_lookup(fs, rel ? rel : "", &dir) != 0 || !dir.is_dir)
        return -1;
    u8 *data = NULL;
    u32 size = 0;
    if (udf_load(fs, &dir, &data, &size) != 0)
        return -1;

    u32 offset = 0;
    while (offset + 38 <= size) {
        struct udf_fid fid;
        int step = udf_fid(data + offset, size - offset, &fid);
        if (step <= 0)
            break;
        if (!fid.is_parent && fid.name[0])
            callback(fid.name, 0, fid.is_dir, context);
        offset += (u32)step;
    }
    kfree(data);
    return 0;
}

static int udf_stat(void *private, const char *rel, int *is_dir, u64 *size)
{
    struct udf_fs *fs = private;
    struct udf_file file;
    if (udf_lookup(fs, rel ? rel : "", &file) != 0)
        return -1;
    if (is_dir)
        *is_dir = file.is_dir;
    if (size)
        *size = file.size;
    return 0;
}

static int udf_read(void *private, const char *rel, u64 offset, void *buffer,
                    u64 capacity, u64 *read_out)
{
    struct udf_fs *fs = private;
    struct udf_file file;
    if (udf_lookup(fs, rel ? rel : "", &file) != 0 || file.is_dir)
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
    if (file.in_icb_size) {
        u64 available = (u64)file.in_icb_size > offset
                            ? (u64)file.in_icb_size - offset : 0;
        u64 copy = count < available ? count : available;
        if (copy)
            memcpy(buffer, file.in_icb + offset, (usize)copy);
        if (copy < count)
            memset((u8 *)buffer + copy, 0, (usize)(count - copy));
        if (read_out)
            *read_out = count;
        return 0;
    }

    u8 *dst = buffer;
    u64 done = 0;
    u64 position = 0;
    for (int i = 0; i < file.extent_count && done < count; i++) {
        u64 length = file.extents[i].length;
        if (offset >= position + length) {
            position += length;
            continue;
        }
        u64 within = offset > position ? offset - position : 0;
        u64 chunk = length - within;
        if (chunk > count - done)
            chunk = count - done;
        if (file.extents[i].sparse) {
            memset(dst + done, 0, (usize)chunk);
        } else if (bf_read_at(&fs->vol, file.extents[i].offset + within,
                              dst + done, (u32)chunk) != 0) {
            return -1;
        }
        done += chunk;
        position += length;
    }
    if (done < count)
        memset(dst + done, 0, (usize)(count - done));
    if (read_out)
        *read_out = count;
    return 0;
}

static const struct vfs_fs_ops g_udf_ops = {
    .name = "udf",
    .list = udf_list,
    .stat = udf_stat,
    .read = udf_read,
    .write = NULL,
    .unlink = NULL,
};

/* Verify the volume recognition sequence at sector 16. */
int udf_probe(const struct blockfs_volume *volume)
{
    if (!volume)
        return -1;
    struct blockfs_volume probe = *volume;
    probe.logical = UDF_BLOCK;
    probe.stride = volume->stride ? volume->stride : UDF_BLOCK;
    probe.data_offset = 0;
    u8 id[5];
    if (bf_read_at(&probe, 16ull * UDF_BLOCK, id, sizeof(id)) != 0)
        return -1;
    if (memcmp(id, "BEA01", 5) && memcmp(id, "NSR02", 5) &&
        memcmp(id, "NSR03", 5))
        return -1;
    return 0;
}

/* Read the anchor volume descriptor pointer, trying the alternate anchors
 * when the primary one is damaged. */
static int udf_anchor(struct udf_fs *fs, const u8 *block, u32 *vds_block,
                      u32 *vds_length)
{
    if (le16(block) != UDF_TAG_AVDP)
        return -1;
    *vds_length = le32(block + 16);
    *vds_block = le32(block + 20);
    return (*vds_block && *vds_length) ? 0 : -1;
}

int udf_mount_volume(const struct blockfs_volume *volume, const char *path)
{
    if (!volume || !path)
        return -1;
    struct udf_fs *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return -1;
    fs->vol = *volume;
    fs->vol.logical = UDF_BLOCK;
    fs->vol.stride = volume->stride ? volume->stride : UDF_BLOCK;
    fs->vol.data_offset = 0;
    fs->block_size = UDF_BLOCK;

    u8 *block = kmalloc(UDF_BLOCK);
    if (!block) {
        kfree(fs);
        return -1;
    }

    u32 vds_block = 0;
    u32 vds_length = 0;
    const u32 anchors[] = {256, 512, 0};
    int anchored = 0;
    for (u32 i = 0; i < sizeof(anchors) / sizeof(anchors[0]); i++) {
        u32 sector = anchors[i];
        if (!sector)
            break;
        if (bf_read_at(&fs->vol, (u64)sector * UDF_BLOCK, block,
                       UDF_BLOCK) == 0 &&
            udf_anchor(fs, block, &vds_block, &vds_length) == 0) {
            anchored = 1;
            break;
        }
    }
    if (!anchored) {
        /* Try the last 2048-byte sector as an alternate anchor. */
        u64 bytes = fs->vol.sectors * 512;
        if (bytes >= UDF_BLOCK &&
            bf_read_at(&fs->vol, bytes - UDF_BLOCK, block, UDF_BLOCK) == 0)
            anchored = udf_anchor(fs, block, &vds_block, &vds_length) == 0;
    }
    if (!anchored) {
        kfree(block);
        kfree(fs);
        return -1;
    }

    u32 fsd_block = 0;
    u16 fsd_part = 0;
    u32 lvd_block_size = 0;
    int have_pd = 0;
    int have_lvd = 0;
    u32 descriptors = vds_length / UDF_BLOCK;
    if (descriptors > 256)
        descriptors = 256;

    for (u32 i = 0; i < descriptors; i++) {
        u32 lba = vds_block + i;
        if (bf_read_at(&fs->vol, (u64)lba * UDF_BLOCK, block,
                       UDF_BLOCK) != 0)
            break;
        u16 tag = le16(block);
        if (tag == UDF_TAG_PD) {
            if (fs->part_count < UDF_MAX_PARTS) {
                fs->parts[fs->part_count].number = le16(block + 22);
                fs->parts[fs->part_count].start = le32(block + 188);
                fs->part_count++;
            }
            have_pd = 1;
        } else if (tag == UDF_TAG_LVD) {
            lvd_block_size = le32(block + 212);
            fsd_block = le32(block + 252);
            fsd_part = le16(block + 256);
            u32 map_len = le32(block + 264);
            u32 map_count = le32(block + 268);
            u32 map_off = 440;
            for (u32 m = 0; m < map_count && m < UDF_MAX_MAPS; m++) {
                if (map_off + 2 > UDF_BLOCK)
                    break;
                u8 type = block[map_off];
                u8 length = block[map_off + 1];
                if (!length || map_off + length > UDF_BLOCK)
                    break;
                if (type == 1 && length >= 6) {
                    fs->maps[fs->map_count++] =
                        le16(block + map_off + 4);
                }
                map_off += length;
                if (map_off >= 440 + map_len)
                    break;
            }
            have_lvd = 1;
        } else if (tag == UDF_TAG_TD || tag == 0) {
            break;
        }
    }
    if (!have_pd || !have_lvd || !fsd_block || !fs->part_count) {
        kfree(block);
        kfree(fs);
        return -1;
    }
    (void)fsd_part;
    if (lvd_block_size >= 512 && lvd_block_size <= 4096 &&
        (lvd_block_size & (lvd_block_size - 1)) == 0) {
        fs->block_size = lvd_block_size;
        fs->vol.logical = lvd_block_size;
        kfree(block);
        block = kmalloc(fs->block_size);
        if (!block) {
            kfree(fs);
            return -1;
        }
    }

    /* The file set descriptor carries the root ICB. */
    u16 root_part = fs->parts[0].number;
    u32 root_lba = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        u16 part = attempt == 0 ? fs->parts[0].number : root_part;
        u64 address = udf_address(fs, part, fsd_block);
        if (bf_read_at(&fs->vol, address, block, fs->block_size) != 0)
            continue;
        if (le16(block) != UDF_TAG_FSD)
            continue;
        root_lba = le32(block + 404);
        root_part = le16(block + 408);
        fs->root_block = root_lba;
        fs->root_part = root_part;
        break;
    }
    if (!fs->root_block) {
        kfree(block);
        kfree(fs);
        return -1;
    }

    struct udf_file root;
    if (udf_file_entry(fs, fs->root_block, fs->root_part, &root, block) ||
        !root.is_dir) {
        kfree(block);
        kfree(fs);
        return -1;
    }
    kfree(block);
    if (vfs_mount(path, "udf", &g_udf_ops, fs) != 0) {
        kfree(fs);
        return -1;
    }
    kprintf("[udf ] volume at %s (block %u, root %u/%u, %u bytes)\n", path,
            fs->block_size, fs->root_block, fs->root_part, root.size);
    return 0;
}
