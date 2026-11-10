#ifndef ZHAOXU_VFS_H
#define ZHAOXU_VFS_H

#include <types.h>

typedef void (*vfs_list_cb)(const char *name, u64 size, int is_dir,
                            void *context);

int vfs_init(void);
int vfs_mount_data(void);
int vfs_list(const char *path, vfs_list_cb callback, void *context);
int vfs_read_all(const char *path, u8 **buffer, u64 *size);
int vfs_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out);
int vfs_stat(const char *path, int *is_dir, u64 *size);
int vfs_write_file(const char *path, const u8 *data, u64 size);
int vfs_unlink(const char *path);
int vfs_open(const char *path);
long vfs_read(int fd, void *buffer, u64 count);
int vfs_close(int fd);
void vfs_close_all(void);

int vfs_fat_mounted(void);
int vfs_data_fallback(void);
const char *vfs_fat_label(void);

/* initramfs (ustar) backed ramfs */
int ramfs_init(void);
int ramfs_list(const char *path, vfs_list_cb callback, void *context);
int ramfs_read_all(const char *path, u8 **buffer, u64 *size);
int ramfs_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out);
int ramfs_stat(const char *path, int *is_dir, u64 *size);

#endif
