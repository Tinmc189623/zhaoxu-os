#ifndef ZHAOXU_EXT4_CORE_H
#define ZHAOXU_EXT4_CORE_H

#include "ext4_env.h"
#include "ext4.h"

#define EXT4_MAGIC 0xEF53u
#define EXT4_ROOT_INO 2u
#define EXT4_LOST_FOUND_INO 11u

#define EXT4_SB_OFFSET 1024u
#define EXT4_INODE_BLOCK_OFF 0x28u
#define EXT4_INODE_EXTENT_BYTES 60u
#define EXT4_INODE_SIZE_OFF 0x04u
#define EXT4_INODE_BLOCKS_OFF 0x1Cu
#define EXT4_INODE_FLAGS_OFF 0x20u
#define EXT4_GD_SIZE32 32u
#define EXT4_GD_SIZE64 64u

#define EXT4_INCOMPAT_FILETYPE 0x0002u
#define EXT4_INCOMPAT_META_BG  0x0010u
#define EXT4_INCOMPAT_EXTENTS  0x0040u
#define EXT4_INCOMPAT_64BIT    0x0080u
#define EXT4_INCOMPAT_INLINE_DATA 0x8000u
#define EXT4_INCOMPAT_ENCRYPT  0x10000u

#define EXT4_RO_SPARSE_SUPER   0x0001u
#define EXT4_RO_EXTRA_ISIZE    0x0040u
#define EXT4_RO_METADATA_CSUM  0x0400u

#define EXT4_EXTENTS_FL 0x00080000u
#define EXT4_EXTENT_MAGIC 0xF30Au

#define EXT4_S_IFREG 0100000u
#define EXT4_S_IFDIR 0040000u
#define EXT4_FT_REG_FILE 1u
#define EXT4_FT_DIR 2u

struct ext4_fs {
    int mounted;
    int writable;
    int disk;
    u64 volume_base;
    u32 block_size;
    u32 sectors_per_block;
    u32 inodes_count;
    u32 blocks_count;
    u32 free_blocks;
    u32 free_inodes;
    u32 first_data_block;
    u32 blocks_per_group;
    u32 inodes_per_group;
    u32 inode_size;
    u32 desc_size;
    u32 feature_incompat;
    u32 feature_ro_compat;
    u32 first_ino;
    u32 groups;
    u32 gdt_block;
    char label[17];
    u32 block_hint;
    u32 inode_hint;
};

struct ext4_inode {
    u8 raw[256];
};

struct ext4_extent_path {
    u64 start;
    u32 count;
};

struct ext4_dirent_info {
    u32 inode;
    u32 rec_len;
    u32 offset;
    u8 name_len;
    u8 type;
    char name[260];
};

extern struct ext4_fs g_ext4;

/* Little-endian accessors shared by every core file.  Inline so the split
 * cannot drift into several subtly different static copies. */
static inline u16 ext4_le16(const u8 *p)
{
    return (u16)(p[0] | ((u16)p[1] << 8));
}

static inline u32 ext4_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static inline u64 ext4_le64(const u8 *p)
{
    return (u64)ext4_le32(p) | ((u64)ext4_le32(p + 4) << 32);
}

static inline u32 ext4_align4(u32 value)
{
    return (value + 3u) & ~3u;
}

static inline void ext4_put16(u8 *p, u16 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}

static inline void ext4_put32(u8 *p, u32 value)
{
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
    p[2] = (u8)(value >> 16);
    p[3] = (u8)(value >> 24);
}

/* ext4_mount.c: block access and inode/group-descriptor reads */
int ext4_read_block(u64 block, void *buffer);
int ext4_write_block(u64 block, const void *buffer);
int ext4_read_inode_raw(u32 ino, u8 *inode);
int ext4_read_group_desc(u32 group, u8 *desc);

/* ext4_alloc.c: group descriptor and inode helpers */
u32 ext4_gd_inode_table(const u8 *desc);
u16 ext4_gd_used_dirs(const u8 *desc);
void ext4_gd_set_used_dirs(u8 *desc, u16 value);
int ext4_write_group_desc(u32 group, const u8 *desc);
int ext4_write_inode_raw(u32 ino, const u8 *inode);
int ext4_alloc_run(u32 count, u32 *first);
void ext4_free_run(u32 first, u32 count);
void ext4_bitmap_invalidate(void);
int ext4_alloc_inode(u32 *ino);
void ext4_free_inode(u32 ino);
int ext4_truncate(u32 ino);
int ext4_update_super(void);
int ext4_sync(void);
int inode_is_dir(const u8 *inode);
u32 inode_size(const u8 *inode);
void inode_set_size(u8 *inode, u32 size);
void inode_init_extents(u8 *inode);
int inode_extent_append(u8 *inode, u32 logical, u32 first, u32 count);
void inode_clear_extents(u8 *inode);

/* ext4_extent.c */
int ext4_map_extent(const u8 *inode, u32 logical,
                    struct ext4_extent_path *path);
int ext4_read_inode_data(const u8 *inode, u64 offset, void *buffer,
                         u64 length, u64 *read_out);

/* ext4_dir.c */
int ext4_dir_next(const u8 *block, u32 *offset,
                  struct ext4_dirent_info *info);
int ext4_inode_dir_find(const u8 *inode, const char *name, u32 *ino,
                        u8 *type);
int ext4_lookup(const char *path, u32 *ino);
int walk_path(const char *path, u32 *ino, u8 *type);

/* ext4_write.c */
int ext4_dir_add(u32 parent, const char *name, u32 child, u8 type);
int ext4_dir_remove(u32 parent, const char *name);
int ext4_create_node(u32 parent, const char *name, u16 mode, u8 type,
                     u32 *ino_out);

/* ext4_format.c */
int fs_has_super(u32 group);

#endif
