#ifndef ZHAOXU_VFS_H
#define ZHAOXU_VFS_H

#include <types.h>

/* Largest file a single write may create through the VFS.  The ext4 service
 * buffers one file in an 8 MiB shared arena and commits it on the last chunk,
 * so the kernel must reject anything larger before streaming chunks into it. */
#define VFS_WRITE_MAX (8u * 1024u * 1024u)

#define VFS_PATH_MAX 256
#define VFS_MAX_MOUNTS 32
#define VFS_MAX_FILES 16
#define VFS_FD_BASE 3

typedef void (*vfs_list_cb)(const char *name, u64 size, int is_dir,
                            void *context);

/* A mounted filesystem.  Paths handed to these callbacks are relative to the
 * mount point; an empty string means the mount root itself.  `private` is the
 * per-mount instance created by the filesystem. */
struct vfs_fs_ops {
    const char *name;
    int (*list)(void *private, const char *rel, vfs_list_cb callback,
                void *context);
    int (*stat)(void *private, const char *rel, int *is_dir, u64 *size);
    int (*read)(void *private, const char *rel, u64 offset, void *buffer,
                u64 capacity, u64 *read_out);
    int (*write)(void *private, const char *rel, const u8 *data, u64 size);
    int (*unlink)(void *private, const char *rel);
};

int vfs_init(void);
int vfs_mount_data(void);
int vfs_mount(const char *path, const char *type,
              const struct vfs_fs_ops *ops, void *private);
int vfs_umount(const char *path);

int vfs_list(const char *path, vfs_list_cb callback, void *context);
int vfs_read_all(const char *path, u8 **buffer, u64 *size);
int vfs_read_at(const char *path, u64 offset, u8 *dst, u64 cap,
                u64 *read_out);
int vfs_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out);
int vfs_stat(const char *path, int *is_dir, u64 *size);
int vfs_write_file(const char *path, const u8 *data, u64 size);
int vfs_unlink(const char *path);
int vfs_open(const char *path);
long vfs_read(int fd, void *buffer, u64 count);
int vfs_close(int fd);
void vfs_close_all(void);

int vfs_data_mounted(void);
int vfs_data_fallback(void);
const char *vfs_data_fs(void);
const char *vfs_data_label(void);

int vfs_mount_count(void);
const char *vfs_mount_path(int index);
const char *vfs_mount_type(int index);
int vfs_mounts_text(char *buffer, usize capacity);

/* initramfs (ustar) backed read-only root filesystem. */
int ramfs_init(void);
int ramfs_mount_root(void);
int ramfs_list(const char *path, vfs_list_cb callback, void *context);
int ramfs_read_all(const char *path, u8 **buffer, u64 *size);
int ramfs_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out);
int ramfs_read_at(const char *path, u64 offset, u8 *dst, u64 cap,
                  u64 *read_out);
int ramfs_stat(const char *path, int *is_dir, u64 *size);

/* In-memory filesystems. */
int tmpfs_mount(const char *path, const char *type);

/* Writable rootfs (initramfs lower + ramfs copy-up) and the writable ramfs
 * instance it uses. */
int rootfs_mount(void);

/* Synthetic and stackable filesystems. */
int procfs_mount(void);
int sysfs_mount(void);
int debugfs_mount(void);
int efivarfs_mount(void);
int overlayfs_mount(void);
int ecryptfs_mount(void);

/* Block-backed filesystems.  Each probe validates the on-disk signature and
 * each mount creates an independent per-volume instance. */
struct blockfs_volume;
int vfat_probe(const struct blockfs_volume *volume);
int vfat_mount_volume(const struct blockfs_volume *volume, const char *path);
int ntfs_probe(const struct blockfs_volume *volume);
int ntfs_mount_volume(const struct blockfs_volume *volume, const char *path);
int exfat_probe(const struct blockfs_volume *volume);
int exfat_mount_volume(const struct blockfs_volume *volume, const char *path);
int iso9660_probe(const struct blockfs_volume *volume);
int iso9660_mount_volume(const struct blockfs_volume *volume,
                         const char *path);
int cdfs_probe(const struct blockfs_volume *volume);
int cdfs_mount_volume(const struct blockfs_volume *volume, const char *path);
int udf_probe(const struct blockfs_volume *volume);
int udf_mount_volume(const struct blockfs_volume *volume, const char *path);
int ubifs_probe(const struct blockfs_volume *volume);
int ubifs_mount_volume(const struct blockfs_volume *volume,
                       const char *path);

/* Probe every disk and partition and mount the filesystems found under
 * /media.  Called once after the Ring3 block service is ready. */
void media_probe_all(void);

#endif
