#ifndef ZHAOXU_EXT4_INTERNAL_H
#define ZHAOXU_EXT4_INTERNAL_H

#include <ext4.h>
#include <types.h>

#define EXT4_MAGIC 0xEF53u
#define EXT4_ROOT_INO 2u
#define EXT4_LOST_FOUND_INO 11u

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

/* ext4.c: read side, shared helpers */
int ext4_read_block(u64 block, void *buffer);
int ext4_write_block(u64 block, const void *buffer);
int ext4_read_inode_raw(u32 ino, u8 *inode);
int ext4_read_group_desc(u32 group, u8 *desc);
int ext4_map_extent(const u8 *inode, u32 logical, struct ext4_extent_path *path);
int ext4_read_inode_data(const u8 *inode, u64 offset, void *buffer, u64 length,
                         u64 *read_out);
int ext4_dir_next(const u8 *block, u32 *offset, struct ext4_dirent_info *info);
int ext4_lookup(const char *path, u32 *ino);
int ext4_inode_dir_find(const u8 *inode, const char *name, u32 *ino,
                        u8 *type);
void ext4_put16(u8 *p, u16 value);
void ext4_put32(u8 *p, u32 value);

/* ext4_write.c */
int ext4_write_group_desc(u32 group, const u8 *desc);
int ext4_update_super(void);
int ext4_sync(void);
int ext4_alloc_run(u32 count, u32 *first);
void ext4_free_run(u32 first, u32 count);
int ext4_alloc_inode(u32 *ino);
void ext4_free_inode(u32 ino);
int ext4_write_inode_raw(u32 ino, const u8 *inode);
int ext4_dir_add(u32 parent, const char *name, u32 child, u8 type);
int ext4_dir_remove(u32 parent, const char *name);
int ext4_create_node(u32 parent, const char *name, u16 mode, u8 type,
                     u32 *ino_out);
int ext4_fill_file(u32 ino, const u8 *data, u64 size);
int ext4_truncate(u32 ino);

#endif
