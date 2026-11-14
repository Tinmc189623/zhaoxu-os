#ifndef ZHAOXU_BLOCKFS_H
#define ZHAOXU_BLOCKFS_H

#include <types.h>

/* A block filesystem sees a contiguous volume described in 512-byte device
 * sectors.  `logical` is the filesystem's own block size (512 for disks,
 * 2048 for ISO 9660/CDFS, 2352 for raw CD sectors) and `data_offset` skips
 * the sync/header bytes that a raw CD sector carries before user data. */
struct blockfs_volume {
    int disk;
    u64 base;
    u64 sectors;
    u32 logical;
    u32 stride;
    u32 data_offset;
};

#define BF_MAX_VOLUMES 16

/* Read `count` logical blocks of `volume->logical` bytes. */
int bf_read(const struct blockfs_volume *volume, u64 lba, u32 count,
            void *out);

/* Byte-addressed read inside the volume; handles unaligned offsets. */
int bf_read_at(const struct blockfs_volume *volume, u64 offset, void *out,
               u32 length);

/* Enumerate the whole disk and the partitions of its MBR/GPT label.  The
 * whole-disk superfloppy candidate is emitted first, followed by partitions
 * in table order. */
int bf_scan_volumes(int disk, struct blockfs_volume *out, int max);

/* Little-endian helpers shared by the on-disk parsers. */
u16 bf_le16(const u8 *p);
u32 bf_le24(const u8 *p);
u32 bf_le32(const u8 *p);
u64 bf_le64(const u8 *p);

/* Convert an on-disk UTF-16LE string to UTF-8.  Invalid surrogates are
 * replaced by U+FFFD so a corrupt name can never escape as a raw byte. */
u32 bf_utf16_to_utf8(const u16 *source, u32 units, char *out, u32 capacity);

/* Case-insensitive (ASCII plus simple Latin-1) comparison used by filesystem
 * name lookups; returns 1 when equal. */
int bf_name_equal(const char *a, const char *b);

#endif
