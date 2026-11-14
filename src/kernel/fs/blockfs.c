/*
 * Zhaoxu OS - block filesystem volume layer.
 *
 * Every on-disk filesystem in the kernel shares this small layer: it turns a
 * disk plus a byte range into logical blocks, copes with CD sector layouts
 * (2048-byte user data inside 2352-byte raw sectors) and enumerates MBR,
 * GPT and extended-partition volumes.  No filesystem code touches the block
 * device facade directly, which keeps the drivers in Ring 3 and the parsers
 * in the kernel.
 */
#include <block.h>
#include <blockfs.h>
#include <mem.h>
#include <string.h>
#include <types.h>

/* Read one logical block through the 512-byte device facade.  A block whose
 * physical stride is larger than the payload (raw CD) is read whole and the
 * caller receives only the data area. */
static int read_block_raw(const struct blockfs_volume *volume, u64 lba,
                          void *out)
{
    u32 stride = volume->stride ? volume->stride : volume->logical;
    u32 sectors = (stride + 511) / 512;
    u64 device_lba = volume->base + lba * sectors;

    if (!volume->data_offset && volume->logical == stride) {
        if (device_lba + sectors > volume->base + volume->sectors)
            return -1;
        return block_disk_read(volume->disk, device_lba, sectors, out);
    }
    if (device_lba + sectors > volume->base + volume->sectors)
        return -1;

    /* block_disk_read always transfers whole 512-byte sectors, so the
     * bounce buffer must hold sectors*512 bytes, not just the stride.  A
     * short buffer here overflowed the kernel heap header for CD volumes
     * whose logical block size is not a multiple of 512. */
    u8 *bounce = kmalloc((usize)sectors * 512u);
    if (!bounce)
        return -1;
    int result = block_disk_read(volume->disk, device_lba, sectors, bounce);
    if (result == 0)
        memcpy(out, bounce + volume->data_offset, volume->logical);
    kfree(bounce);
    return result;
}

int bf_read(const struct blockfs_volume *volume, u64 lba, u32 count,
            void *out)
{
    if (!volume || (!out && count) || !volume->logical)
        return -1;
    u32 stride = volume->stride ? volume->stride : volume->logical;
    u64 blocks = (volume->sectors * 512) / stride;
    if (lba + count > blocks)
        return -1;
    if (!volume->data_offset && volume->logical == stride &&
        (stride % 512) == 0) {
        u64 device_lba = volume->base + lba * (stride / 512);
        u64 device_count = (u64)count * (stride / 512);
        if (device_count > 0xFFFFu)
            return -1;
        return block_disk_read(volume->disk, device_lba,
                               (u32)device_count, out);
    }

    u8 *dst = out;
    for (u32 i = 0; i < count; i++) {
        if (read_block_raw(volume, lba + i, dst + (u64)i * volume->logical))
            return -1;
    }
    return 0;
}

int bf_read_at(const struct blockfs_volume *volume, u64 offset, void *out,
               u32 length)
{
    if (!volume || (!out && length) || !volume->logical)
        return -1;
    /* `sectors` is always counted in device 512-byte units; payload offsets
     * for CD volumes are smaller than that byte count, so this bound is
     * conservative for every logical layout. */
    u64 total = volume->sectors * 512;
    if (offset > total || length > total - offset)
        return -1;

    u8 *dst = out;
    if (!length)
        return 0;

    /* Fast path for ordinary disk volumes: a 512-byte aligned request maps
     * straight to a multi-sector device read. */
    if (volume->logical == 512 && !volume->data_offset &&
        (offset % 512) == 0 && (length % 512) == 0) {
        u64 lba = offset / 512;
        u64 count = length / 512;
        if (count > 0xFFFFu)
            return -1;
        return block_disk_read(volume->disk, volume->base + lba,
                               (u32)count, out);
    }

    while (length) {
        u64 lba = offset / volume->logical;
        u32 within = (u32)(offset % volume->logical);
        u32 chunk = volume->logical - within;
        if (chunk > length)
            chunk = length;

        if (within == 0 && chunk == volume->logical) {
            if (bf_read(volume, lba, 1, dst) != 0)
                return -1;
        } else {
            u8 *bounce = kmalloc(volume->logical);
            if (!bounce)
                return -1;
            int result = bf_read(volume, lba, 1, bounce);
            if (result == 0)
                memcpy(dst, bounce + within, chunk);
            kfree(bounce);
            if (result != 0)
                return -1;
        }
        dst += chunk;
        offset += chunk;
        length -= chunk;
    }
    return 0;
}

u16 bf_le16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

u32 bf_le24(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16);
}

u32 bf_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

u64 bf_le64(const u8 *p)
{
    return (u64)bf_le32(p) | ((u64)bf_le32(p + 4) << 32);
}

/* Encode one Unicode scalar as UTF-8 and return the byte count. */
static u32 utf8_put(u32 code, char *out, u32 capacity)
{
    if (code <= 0x7F) {
        if (capacity < 1)
            return 0;
        out[0] = (char)code;
        return 1;
    }
    if (code <= 0x7FF) {
        if (capacity < 2)
            return 0;
        out[0] = (char)(0xC0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    }
    if (code <= 0xFFFF) {
        if (capacity < 3)
            return 0;
        out[0] = (char)(0xE0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        out[2] = (char)(0x80 | (code & 0x3F));
        return 3;
    }
    if (capacity < 4)
        return 0;
    out[0] = (char)(0xF0 | (code >> 18));
    out[1] = (char)(0x80 | ((code >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((code >> 6) & 0x3F));
    out[3] = (char)(0x80 | (code & 0x3F));
    return 4;
}

u32 bf_utf16_to_utf8(const u16 *source, u32 units, char *out, u32 capacity)
{
    u32 used = 0;
    if (!out || !capacity)
        return 0;
    for (u32 i = 0; i < units; i++) {
        u32 code = source[i];
        if (code >= 0xD800 && code <= 0xDBFF && i + 1 < units &&
            source[i + 1] >= 0xDC00 && source[i + 1] <= 0xDFFF) {
            code = 0x10000 + ((code - 0xD800) << 10) +
                   (source[i + 1] - 0xDC00);
            i++;
        } else if (code >= 0xD800 && code <= 0xDFFF) {
            code = 0xFFFD;
        }
        if (code == 0)
            break;
        u32 written = utf8_put(code, out + used, capacity - used);
        if (!written)
            break;
        used += written;
    }
    if (used < capacity)
        out[used] = 0;
    return used;
}

int bf_name_equal(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++;
        unsigned char y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z')
            x = (unsigned char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = (unsigned char)(y - 'A' + 'a');
        if (x != y)
            return 0;
    }
    return *a == 0 && *b == 0;
}

/* Add one partition candidate after validating its sector range. */
static int add_volume(struct blockfs_volume *out, int max, int count,
                      int disk, u64 base, u64 sectors)
{
    if (count >= max || !sectors || base >= sectors)
        return count;
    u64 disk_sectors = block_disk_sectors(disk);
    if (base + sectors > disk_sectors)
        sectors = disk_sectors > base ? disk_sectors - base : 0;
    if (!sectors)
        return count;
    out[count].disk = disk;
    out[count].base = base;
    out[count].sectors = sectors;
    out[count].logical = 512;
    out[count].stride = 512;
    out[count].data_offset = 0;
    return count + 1;
}

/* Walk a GPT entry array.  Entries are 128..4096 bytes wide and the array
 * may span many sectors, so each accessed sector is read independently. */
static int scan_gpt(int disk, const u8 *header, struct blockfs_volume *out,
                    int max, int count)
{
    u64 entries_lba = bf_le64(header + 72);
    u32 entries = bf_le32(header + 80);
    u32 entry_size = bf_le32(header + 84);
    u64 disk_sectors = block_disk_sectors(disk);

    if (entry_size < 128 || entry_size > 4096 || entries > 256 ||
        entries_lba == 0)
        return count;

    u8 *sector = kmalloc(512);
    if (!sector)
        return count;
    for (u32 i = 0; i < entries && count < max; i++) {
        u64 offset = (u64)i * entry_size;
        u64 lba = entries_lba + offset / 512;
        u32 within = (u32)(offset % 512);
        if (within + 128 > 512 || lba >= disk_sectors)
            continue;
        if (block_disk_read(disk, lba, 1, sector) != 0)
            continue;
        const u8 *entry = sector + within;
        int nonzero = 0;
        for (int k = 0; k < 16; k++)
            nonzero |= entry[k];
        if (!nonzero)
            continue;
        /* A protective entry that describes the whole disk is not usable as
         * a filesystem volume. */
        u64 first = bf_le64(entry + 32);
        u64 last = bf_le64(entry + 40);
        if (last < first || first == 0)
            continue;
        count = add_volume(out, max, count, disk, first, last - first + 1);
    }
    kfree(sector);
    return count;
}

/* Scan the four primary MBR slots plus a bounded chain of extended boot
 * records (EBR), which is how legacy disks expose more than four volumes. */
static int scan_mbr(int disk, const u8 *mbr, struct blockfs_volume *out,
                    int max, int count)
{
    u64 disk_sectors = block_disk_sectors(disk);
    u64 extended = 0;
    u8 *sector = kmalloc(512);
    if (!sector)
        return count;

    for (int i = 0; i < 4; i++) {
        const u8 *entry = mbr + 446 + i * 16;
        u8 type = entry[4];
        u32 start = bf_le32(entry + 8);
        u32 length = bf_le32(entry + 12);
        if (type == 0 || length == 0 || start >= disk_sectors)
            continue;
        if (type == 0x05 || type == 0x0F || type == 0x85) {
            extended = start;
            continue;
        }
        count = add_volume(out, max, count, disk, start, length);
    }

    u64 ebr_lba = extended;
    for (int link = 0; link < 8 && ebr_lba && count < max; link++) {
        if (block_disk_read(disk, ebr_lba, 1, sector) != 0)
            break;
        if (sector[510] != 0x55 || sector[511] != 0xAA)
            break;
        const u8 *entry = sector + 446;
        u8 type = entry[4];
        u32 relative = bf_le32(entry + 8);
        u32 length = bf_le32(entry + 12);
        if (type && length && relative)
            count = add_volume(out, max, count, disk, ebr_lba + relative,
                               length);
        const u8 *next = sector + 446 + 16;
        u32 next_relative = bf_le32(next + 8);
        u8 next_type = next[4];
        if (!next_type || !next_relative)
            break;
        ebr_lba = extended + next_relative;
    }

    kfree(sector);
    return count;
}

int bf_scan_volumes(int disk, struct blockfs_volume *out, int max)
{
    if (!out || max <= 0)
        return -1;
    u64 sectors = block_disk_sectors(disk);
    if (sectors < 2)
        return -1;

    int count = add_volume(out, max, 0, disk, 0, sectors);
    u8 *sector = kmalloc(512);
    if (!sector)
        return count;
    if (block_disk_read(disk, 0, 1, sector) != 0) {
        kfree(sector);
        return count;
    }
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        kfree(sector);
        return count;
    }

    int protective = 0;
    for (int i = 0; i < 4; i++) {
        if (sector[446 + i * 16 + 4] == 0xEE)
            protective = 1;
    }

    u8 *header = kmalloc(512);
    if (!header) {
        kfree(sector);
        return count;
    }
    if (protective && block_disk_read(disk, 1, 1, header) == 0 &&
        memcmp(header, "EFI PART", 8) == 0) {
        count = scan_gpt(disk, header, out, max, count);
    } else {
        count = scan_mbr(disk, sector, out, max, count);
    }
    kfree(header);
    kfree(sector);
    return count;
}
