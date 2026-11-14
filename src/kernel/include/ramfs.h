#ifndef ZHAOXU_RAMFS_H
#define ZHAOXU_RAMFS_H

#include <types.h>
#include <vfs.h>

/* Writable in-memory filesystem used by ramfs mounts and as the copy-up
 * layer of rootfs.  Paths passed to these helpers are relative to the
 * instance root; an empty string names the root directory. */
struct wramfs;

struct wramfs *wramfs_create(void);
void wramfs_destroy(struct wramfs *fs);
int wramfs_list(struct wramfs *fs, const char *rel, vfs_list_cb callback,
                void *context);
int wramfs_stat(struct wramfs *fs, const char *rel, int *is_dir, u64 *size);
int wramfs_read(struct wramfs *fs, const char *rel, u64 offset, void *buffer,
                u64 capacity, u64 *read_out);
int wramfs_write(struct wramfs *fs, const char *rel, const u8 *data,
                 u64 size);
int wramfs_unlink(struct wramfs *fs, const char *rel);

/* Mount a fresh ramfs at `path`. */
int ramfs_mount_at(const char *path, const char *type);

#endif
