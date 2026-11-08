#ifndef ZHAOXU_FAT32_H
#define ZHAOXU_FAT32_H

#include <types.h>
#include <vfs.h>

int fat32_mount(void);
int fat32_mounted(void);
const char *fat32_label(void);
int fat32_list(const char *path, vfs_list_cb callback, void *context);
int fat32_read_all(const char *path, u8 **buffer, u64 *size);
int fat32_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out);
int fat32_stat(const char *path, int *is_dir, u64 *size);

#endif
