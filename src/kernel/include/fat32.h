#ifndef ZHAOXU_FAT32_H
#define ZHAOXU_FAT32_H

#include <types.h>
#include <vfs.h>

#define FAT32_MAX_FILE_SIZE (16 * 1024 * 1024)

int fat32_mount(void);
int fat32_mounted(void);
const char *fat32_label(void);
int fat32_list(const char *path, vfs_list_cb callback, void *context);
int fat32_read_all(const char *path, u8 **buffer, u64 *size);
int fat32_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out);
int fat32_stat(const char *path, int *is_dir, u64 *size);
int fat32_write_file(const char *path, const u8 *data, u64 size);
int fat32_unlink(const char *path);
int fat32_mkdir(const char *path);
int fat32_mount_at(int disk, u64 volume_base);
int fat32_format_volume(int disk, u64 volume_base, u32 sectors,
                        const char *label);
int fat32_make_esp(int disk, u64 volume_base, u32 sectors, const u8 *bootx64,
                   u64 bootx64_size, const u8 *kernel, u64 kernel_size);
int fat32_make_data(int disk, u64 volume_base, u32 sectors, const u8 *font,
                    u64 font_size);

#endif
