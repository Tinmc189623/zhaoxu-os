/*
 * ext4 core - volume formatter and installed-root builder.
 *
 * Pre-mount: every I/O goes through the disk/base arguments handed to the
 * formatter, never through g_ext4.  Post-mount (ext4_make_root) the normal
 * mounted paths are used.
 */
#include "ext4_core.h"

#include <string.h>
#include <types.h>

#define EXT4_BPG 32768u
#define EXT4_IPG 8192u
#define EXT4_ISIZE 256u

static int raw_write(int disk, u64 base, u64 offset, const void *buffer,
                     u32 length)
{
    return ext4_env()->write(disk, base, offset, buffer, length);
}

static void put_inode(u8 *inode, u16 mode, u16 links, u32 size,
                      u32 block_count, u32 first, u32 count)
{
    memset(inode, 0, 256);
    ext4_put16(inode, mode);
    ext4_put16(inode + 0x1A, links);
    ext4_put32(inode + EXT4_INODE_BLOCKS_OFF, block_count);
    ext4_put32(inode + EXT4_INODE_FLAGS_OFF, EXT4_EXTENTS_FL);
    u8 *node = inode + EXT4_INODE_BLOCK_OFF;
    ext4_put16(node, EXT4_EXTENT_MAGIC);
    ext4_put16(node + 2, 1);
    ext4_put16(node + 4, 4);
    ext4_put16(node + 6, 0);
    ext4_put32(node + 8, 0);
    u8 *entry = node + 12;
    ext4_put32(entry, 0);
    ext4_put16(entry + 4, (u16)count);
    ext4_put16(entry + 6, 0);
    ext4_put32(entry + 8, first);
    ext4_put32(inode + EXT4_INODE_SIZE_OFF, size);
    ext4_put32(inode + 0x6C, 0);
    ext4_put16(inode + 0x80, 32);
}

/* sector is relative to the volume, not to the disk. */
static int zero_sectors(int disk, u64 base, u64 sector, u64 count)
{
    u32 chunk_sectors = 128;
    u8 *zero = ext4_alloc(chunk_sectors * 512);
    if (!zero)
        return -1;
    memset(zero, 0, chunk_sectors * 512);
    int result = 0;
    while (count) {
        u32 chunk = count > chunk_sectors ? chunk_sectors : (u32)count;
        if (raw_write(disk, base, sector * 512, zero,
                      chunk * 512) != 0) {
            result = -1;
            break;
        }
        sector += chunk;
        count -= chunk;
    }
    ext4_free(zero);
    return result;
}

static int fmt_has_super(u32 group)
{
    if (group == 0 || group == 1)
        return 1;
    u32 power = 3;
    for (int base = 3; base <= 7; base += 2) {
        power = (u32)base;
        while (power < group)
            power *= (u32)base;
        if (power == group)
            return 1;
    }
    return 0;
}

int fs_has_super(u32 group)
{
    if (group == 0)
        return 1;
    if (!(g_ext4.feature_ro_compat & EXT4_RO_SPARSE_SUPER))
        return 1;
    return fmt_has_super(group);
}

static int group_inode_blocks(u32 group, u32 gdt_blocks, u32 itable_blocks,
                              u32 *bitmap_block, u32 *inode_bitmap_block,
                              u32 *inode_table_block, u32 *used_blocks)
{
    u64 start = (u64)group * EXT4_BPG;
    u32 cursor = 0;
    if (fmt_has_super(group))
        cursor += 1 + gdt_blocks;
    *bitmap_block = (u32)start + cursor;
    cursor++;
    *inode_bitmap_block = (u32)start + cursor;
    cursor++;
    *inode_table_block = (u32)start + cursor;
    cursor += itable_blocks;
    *used_blocks = cursor;
    return 0;
}

int ext4_format_volume(int disk, u64 volume_base, u32 sectors,
                       const char *label)
{
    u32 block_size = 4096;
    u32 spb = block_size / 512;
    u64 total_blocks = (u64)sectors * 512 / block_size;
    if (total_blocks < 512 || total_blocks > 0xFFFFFFFFull)
        return -1;
    u32 blocks = (u32)total_blocks;
    u32 groups = (blocks + EXT4_BPG - 1) / EXT4_BPG;
    if (groups == 0)
        return -1;
    u32 gdt_bytes = groups * 32;
    u32 gdt_blocks = (gdt_bytes + block_size - 1) / block_size;
    u32 itable_blocks = EXT4_IPG * EXT4_ISIZE / block_size;

    u8 *gdt = ext4_alloc(gdt_blocks * block_size);
    if (!gdt)
        return -1;
    memset(gdt, 0, gdt_blocks * block_size);

    u32 free_blocks = 0;
    u32 free_inodes = groups * EXT4_IPG;

    for (u32 group = 0; group < groups; group++) {
        u64 start64 = (u64)group * EXT4_BPG;
        if (start64 >= blocks)
            break;
        u32 in_group = blocks - (u32)start64;
        if (in_group > EXT4_BPG)
            in_group = EXT4_BPG;

        u32 bitmap_block, inode_bitmap_block, inode_table_block, used;
        group_inode_blocks(group, gdt_blocks, itable_blocks, &bitmap_block,
                           &inode_bitmap_block, &inode_table_block, &used);
        if (used > in_group) {
            ext4_free(gdt);
            return -1;
        }

        u32 reserved = group == 0 ? EXT4_LOST_FOUND_INO : 0;
        u32 group_free_blocks = in_group - used;
        u32 group_free_inodes = EXT4_IPG - reserved;
        free_blocks += group_free_blocks;
        free_inodes -= reserved;

        u8 *desc = gdt + (u64)group * 32;
        ext4_put32(desc, bitmap_block);
        ext4_put32(desc + 4, inode_bitmap_block);
        ext4_put32(desc + 8, inode_table_block);
        ext4_put16(desc + 0x0C, (u16)group_free_blocks);
        ext4_put16(desc + 0x0E, (u16)group_free_inodes);
        ext4_put16(desc + 0x10, (u16)(group == 0 ? 2 : 0));
    }

    /* Group 0 needs two data blocks for the root and lost+found dirs. */
    u32 bitmap_block0, inode_bitmap0, inode_table0, used0;
    group_inode_blocks(0, gdt_blocks, itable_blocks, &bitmap_block0,
                       &inode_bitmap0, &inode_table0, &used0);
    u32 root_block = used0;
    u32 lost_block = used0 + 1;
    u32 in_group0 = blocks > EXT4_BPG ? EXT4_BPG : blocks;
    if (lost_block >= in_group0) {
        ext4_free(gdt);
        return -1;
    }

    u8 *desc0 = gdt;
    ext4_put16(desc0 + 0x0C,
               (u16)(ext4_le16(desc0 + 0x0C) - 2));
    free_blocks -= 2;

    u8 *bitmap = ext4_alloc(block_size);
    u8 *block = ext4_alloc(block_size);
    u8 *sb = ext4_alloc(1024);
    if (!bitmap || !block || !sb) {
        ext4_free(gdt);
        ext4_free(bitmap);
        ext4_free(block);
        ext4_free(sb);
        return -1;
    }

    /* copy of the superblock */
    memset(sb, 0, 1024);
    ext4_put32(sb + 0x00, groups * EXT4_IPG);
    ext4_put32(sb + 0x04, blocks);
    ext4_put32(sb + 0x08, blocks / 20);
    ext4_put32(sb + 0x0C, free_blocks);
    ext4_put32(sb + 0x10, free_inodes);
    ext4_put32(sb + 0x14, 0);
    ext4_put32(sb + 0x18, 2);
    ext4_put32(sb + 0x1C, 2);
    ext4_put32(sb + 0x20, EXT4_BPG);
    ext4_put32(sb + 0x24, EXT4_BPG);
    ext4_put32(sb + 0x28, EXT4_IPG);
    ext4_put16(sb + 0x34, 0);
    ext4_put16(sb + 0x36, 0xFFFF);
    ext4_put16(sb + 0x38, EXT4_MAGIC);
    ext4_put16(sb + 0x3A, 1);
    ext4_put16(sb + 0x3C, 1);
    ext4_put32(sb + 0x48, 0);
    ext4_put32(sb + 0x4C, 1);
    ext4_put32(sb + 0x54, EXT4_LOST_FOUND_INO);
    ext4_put16(sb + 0x58, EXT4_ISIZE);
    ext4_put32(sb + 0x5C, 0);
    ext4_put32(sb + 0x60, EXT4_INCOMPAT_FILETYPE | EXT4_INCOMPAT_EXTENTS);
    ext4_put32(sb + 0x64, EXT4_RO_SPARSE_SUPER | EXT4_RO_EXTRA_ISIZE);
    static const u8 uuid[16] = {
        0x5A, 0x4F, 0x53, 0x45, 0x58, 0x54, 0x34, 0x00,
        0x9A, 0x4F, 0x53, 0x31, 0x00, 0x00, 0x00, 0x03
    };
    memcpy(sb + 0x68, uuid, 16);
    for (int i = 0; i < 16; i++)
        sb[0x78 + i] = label && label[i] ? (u8)label[i] : 0;
    ext4_put16(sb + 0x5A, 0);
    ext4_put16(sb + 0x15C, 32);
    ext4_put16(sb + 0x15E, 32);

    /* block bitmaps and inode bitmaps */
    for (u32 group = 0; group < groups; group++) {
        u64 start64 = (u64)group * EXT4_BPG;
        u32 in_group = blocks > (u32)start64 ? blocks - (u32)start64 : 0;
        if (in_group > EXT4_BPG)
            in_group = EXT4_BPG;
        if (!in_group)
            break;

        u32 bm, ibm, itb, used;
        group_inode_blocks(group, gdt_blocks, itable_blocks, &bm, &ibm, &itb,
                           &used);
        memset(bitmap, 0, block_size);
        for (u32 i = 0; i < used; i++)
            bitmap[i / 8] |= (u8)(1u << (i % 8));
        for (u32 i = in_group; i < EXT4_BPG; i++)
            bitmap[i / 8] |= (u8)(1u << (i % 8));
        if (group == 0) {
            bitmap[root_block / 8] |= (u8)(1u << (root_block % 8));
            bitmap[lost_block / 8] |= (u8)(1u << (lost_block % 8));
        }
        if (raw_write(disk, volume_base, (u64)bm * block_size, bitmap,
                      block_size) != 0) {
            ext4_free(gdt);
            ext4_free(bitmap);
            ext4_free(block);
            ext4_free(sb);
            return -1;
        }

        memset(bitmap, 0xFF, block_size);
        for (u32 i = 0; i < EXT4_IPG; i++)
            bitmap[i / 8] &= (u8)~(1u << (i % 8));
        if (group == 0) {
            for (u32 i = 0; i < EXT4_LOST_FOUND_INO; i++)
                bitmap[i / 8] |= (u8)(1u << (i % 8));
        }
        if (raw_write(disk, volume_base, (u64)ibm * block_size, bitmap,
                      block_size) != 0) {
            ext4_free(gdt);
            ext4_free(bitmap);
            ext4_free(block);
            ext4_free(sb);
            return -1;
        }

        if (zero_sectors(disk, volume_base, (u64)itb * spb,
                         (u64)itable_blocks * spb) != 0) {
            ext4_free(gdt);
            ext4_free(bitmap);
            ext4_free(block);
            ext4_free(sb);
            return -1;
        }
    }

    /* root and lost+found directory blocks */
    memset(block, 0, block_size);
    u8 *entry = block;
    ext4_put32(entry, EXT4_ROOT_INO);
    ext4_put16(entry + 4, 12);
    entry[6] = 1;
    entry[7] = EXT4_FT_DIR;
    entry[8] = '.';
    entry = block + 12;
    ext4_put32(entry, EXT4_ROOT_INO);
    ext4_put16(entry + 4, 12);
    entry[6] = 2;
    entry[7] = EXT4_FT_DIR;
    entry[8] = '.';
    entry[9] = '.';
    entry = block + 24;
    ext4_put32(entry, EXT4_LOST_FOUND_INO);
    ext4_put16(entry + 4, (u16)(block_size - 24));
    entry[6] = 10;
    entry[7] = EXT4_FT_DIR;
    memcpy(entry + 8, "lost+found", 10);
    if (raw_write(disk, volume_base, (u64)root_block * block_size, block,
                  block_size) != 0) {
        ext4_free(gdt);
        ext4_free(bitmap);
        ext4_free(block);
        ext4_free(sb);
        return -1;
    }

    memset(block, 0, block_size);
    entry = block;
    ext4_put32(entry, EXT4_LOST_FOUND_INO);
    ext4_put16(entry + 4, 12);
    entry[6] = 1;
    entry[7] = EXT4_FT_DIR;
    entry[8] = '.';
    entry = block + 12;
    ext4_put32(entry, EXT4_ROOT_INO);
    ext4_put16(entry + 4, (u16)(block_size - 12));
    entry[6] = 2;
    entry[7] = EXT4_FT_DIR;
    entry[8] = '.';
    entry[9] = '.';
    if (raw_write(disk, volume_base, (u64)lost_block * block_size, block,
                  block_size) != 0) {
        ext4_free(gdt);
        ext4_free(bitmap);
        ext4_free(block);
        ext4_free(sb);
        return -1;
    }

    /* inode table entry block (inodes 1..16) */
    memset(block, 0, block_size);
    put_inode(block + 1 * EXT4_ISIZE, (u16)(EXT4_S_IFDIR | 0755), 3,
              block_size, 8, root_block, 1);
    put_inode(block + 10 * EXT4_ISIZE, (u16)(EXT4_S_IFDIR | 0700), 2,
              block_size, 8, lost_block, 1);
    if (raw_write(disk, volume_base, (u64)inode_table0 * block_size, block,
                  block_size) != 0) {
        ext4_free(gdt);
        ext4_free(bitmap);
        ext4_free(block);
        ext4_free(sb);
        return -1;
    }

    /* primary superblock + group descriptors */
    memset(block, 0, block_size);
    memcpy(block + 1024, sb, 1024);
    if (raw_write(disk, volume_base, 0, block, block_size) != 0) {
        ext4_free(gdt);
        ext4_free(bitmap);
        ext4_free(block);
        ext4_free(sb);
        return -1;
    }
    if (raw_write(disk, volume_base, (u64)1 * block_size, gdt,
                  gdt_blocks * block_size) != 0) {
        ext4_free(gdt);
        ext4_free(bitmap);
        ext4_free(block);
        ext4_free(sb);
        return -1;
    }

    /* backup superblocks and group descriptors */
    for (u32 group = 1; group < groups; group++) {
        if (!fmt_has_super(group))
            continue;
        u64 start = (u64)group * EXT4_BPG;
        memcpy(block, sb, 1024);
        memset(block + 1024, 0, block_size - 1024);
        ext4_put16(block + 0x5A, (u16)group);
        if (raw_write(disk, volume_base, start * block_size, block,
                      block_size) != 0)
            break;
        if (raw_write(disk, volume_base, (start + 1) * block_size, gdt,
                      gdt_blocks * block_size) != 0)
            break;
    }

    ext4_free(gdt);
    ext4_free(bitmap);
    ext4_free(block);
    ext4_free(sb);
    const struct ext4_env *env = ext4_env();
    if (!env->flush)
        return 0;
    return env->flush(disk) == 0 ? 0 : -1;
}

int ext4_make_root(int disk, u64 volume_base, u32 sectors, const char *label,
                   const u8 *kernel_elf, u64 kernel_elf_size,
                   const u8 *kernel_bin, u64 kernel_bin_size,
                   const u8 *font, u64 font_size,
                   u32 *bin_block, u32 *bin_blocks)
{
    if (!kernel_elf || !kernel_elf_size || !kernel_bin || !kernel_bin_size)
        return -1;
    if (ext4_format_volume(disk, volume_base, sectors, label) != 0) {
        ext4_logf("[ext4] format failed \n");
        return -1;
    }
    if (ext4_mount_at(disk, volume_base) != 0) {
        ext4_logf("[ext4] mount after format failed \n");
        return -1;
    }
    if (!g_ext4.writable) {
        ext4_logf("[ext4] volume is read-only \n");
        return -1;
    }
    ext4_logf("[ext4] formatted %u blocks, %u groups, label %s \n",
              g_ext4.blocks_count, g_ext4.groups, g_ext4.label);

    if (ext4_write_file("/KERNEL.ELF", kernel_elf, kernel_elf_size) != 0) {
        ext4_logf("[ext4] KERNEL.ELF write failed \n");
        return -1;
    }
    if (ext4_write_file("/KERNEL.BIN", kernel_bin, kernel_bin_size) != 0) {
        ext4_logf("[ext4] KERNEL.BIN write failed \n");
        return -1;
    }
    if (font && font_size > 12 &&
        ext4_write_file("/WQY.TTC", font, font_size) != 0) {
        ext4_logf("[ext4] WQY.TTC write failed \n");
        return -1;
    }

    u32 first = 0;
    u32 count = 0;
    if (ext4_file_extent("/KERNEL.BIN", &first, &count) != 0) {
        ext4_logf("[ext4] KERNEL.BIN is not one extent \n");
        return -1;
    }
    if ((u64)count * g_ext4.block_size < kernel_bin_size)
        return -1;
    if (bin_block)
        *bin_block = first;
    if (bin_blocks)
        *bin_blocks = count;
    return ext4_sync();
}
