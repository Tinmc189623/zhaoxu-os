#ifndef ZHAOXU_EXT4_H
#define ZHAOXU_EXT4_H

#include <types.h>

#define EXT4_MAX_FILE_SIZE (16 * 1024 * 1024)

typedef void (*ext4_list_cb)(const char *name, u64 size, int is_dir,
                             void *context);

struct ext4_state {
    u8 bytes[256];
};

int ext4_mount(void);
int ext4_mounted(void);
const char *ext4_label(void);
int ext4_list(const char *path, ext4_list_cb callback, void *context);
int ext4_read_all(const char *path, u8 **buffer, u64 *size);
int ext4_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out);
int ext4_read_at(const char *path, u64 offset, u8 *dst, u64 cap,
                 u64 *read_out);
int ext4_stat(const char *path, int *is_dir, u64 *size);
int ext4_write_file(const char *path, const u8 *data, u64 size);
int ext4_write_file_contig(const char *path, const u8 *data, u64 size,
                           u32 *first, u32 *blocks);
int ext4_unlink(const char *path);
int ext4_sync(void);
int ext4_mount_at(int disk, u64 volume_base);
int ext4_format_volume(int disk, u64 volume_base, u32 sectors,
                       const char *label);
int ext4_make_root(int disk, u64 volume_base, u32 sectors, const char *label,
                   const u8 *kernel_elf, u64 kernel_elf_size,
                   const u8 *kernel_bin, u64 kernel_bin_size,
                   const u8 *font, u64 font_size,
                   u32 *bin_block, u32 *bin_blocks);
int ext4_file_extent(const char *path, u32 *start_block, u32 *blocks);
struct ext4_state ext4_state_save(void);
void ext4_state_restore(struct ext4_state state);

#endif
