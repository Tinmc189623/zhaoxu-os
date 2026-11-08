/*
 * Zhaoxu OS - virtual file system
 *
 * The initramfs ramfs provides the root filesystem.  A FAT32 volume, when
 * present, is mounted at /mnt.  When no FAT32 volume exists, /mnt falls
 * back to the embedded copy of the data volume under /data in the ramfs,
 * so ISO-only and disk-less boots still expose the same files.
 * File descriptors expose read-only access to either backend.
 */
#include <fat32.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define VFS_MAX_FILES 8
#define VFS_FD_BASE 3

struct vfs_file {
    int used;
    u8 *data;
    u64 size;
    u64 offset;
};

static struct vfs_file g_files[VFS_MAX_FILES];
static int g_fat_mounted;

static int path_is_mnt(const char *path)
{
    if (!path)
        return 0;
    if (strncmp(path, "/mnt", 4) != 0)
        return 0;
    return path[4] == 0 || path[4] == '/';
}

/*
 * With no FAT32 volume, translate /mnt/... to the embedded mirror at
 * /data/....  Returns 1 when the translation was written to buffer.
 */
static int mnt_fallback_path(const char *path, char *buffer, usize capacity)
{
    if (g_fat_mounted || !path_is_mnt(path))
        return 0;
    int written = ksnprintf(buffer, capacity, "/data%s", path + 4);
    return written > 0 && (usize)written < capacity;
}

int vfs_init(void)
{
    memset(g_files, 0, sizeof(g_files));
    g_fat_mounted = 0;
    return ramfs_init();
}

int vfs_mount_data(void)
{
    if (fat32_mount() != 0)
        return -1;
    g_fat_mounted = 1;
    return 0;
}

int vfs_list(const char *path, vfs_list_cb callback, void *context)
{
    if (!path || !*path)
        path = "/";

    char alt[300];
    if (mnt_fallback_path(path, alt, sizeof(alt)))
        return ramfs_list(alt, callback, context);
    if (g_fat_mounted && path_is_mnt(path)) {
        const char *inner = path + 4;
        if (!*inner)
            inner = "/";
        return fat32_list(inner, callback, context);
    }
    return ramfs_list(path, callback, context);
}

int vfs_read_all(const char *path, u8 **buffer, u64 *size)
{
    if (!path)
        return -1;
    char alt[300];
    if (mnt_fallback_path(path, alt, sizeof(alt)))
        return ramfs_read_all(alt, buffer, size);
    if (g_fat_mounted && path_is_mnt(path)) {
        const char *inner = path + 4;
        if (!*inner)
            inner = "/";
        return fat32_read_all(inner, buffer, size);
    }
    return ramfs_read_all(path, buffer, size);
}

int vfs_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out)
{
    if (!path)
        return -1;
    char alt[300];
    if (mnt_fallback_path(path, alt, sizeof(alt)))
        return ramfs_read_into(alt, dst, cap, read_out);
    if (g_fat_mounted && path_is_mnt(path)) {
        const char *inner = path + 4;
        if (!*inner)
            inner = "/";
        return fat32_read_into(inner, dst, cap, read_out);
    }
    return ramfs_read_into(path, dst, cap, read_out);
}

int vfs_stat(const char *path, int *is_dir, u64 *size)
{
    if (!path)
        return -1;
    char alt[300];
    if (mnt_fallback_path(path, alt, sizeof(alt)))
        return ramfs_stat(alt, is_dir, size);
    if (g_fat_mounted && path_is_mnt(path)) {
        const char *inner = path + 4;
        if (!*inner)
            inner = "/";
        return fat32_stat(inner, is_dir, size);
    }
    return ramfs_stat(path, is_dir, size);
}

int vfs_open(const char *path)
{
    u8 *data = NULL;
    u64 size = 0;
    if (vfs_read_all(path, &data, &size) != 0)
        return -1;

    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (g_files[i].used)
            continue;
        g_files[i].used = 1;
        g_files[i].data = data;
        g_files[i].size = size;
        g_files[i].offset = 0;
        return i + VFS_FD_BASE;
    }

    kfree(data);
    return -1;
}

long vfs_read(int fd, void *buffer, u64 count)
{
    if (fd < VFS_FD_BASE || !buffer)
        return -1;
    int index = fd - VFS_FD_BASE;
    if (index < 0 || index >= VFS_MAX_FILES || !g_files[index].used)
        return -1;

    struct vfs_file *file = &g_files[index];
    if (file->offset >= file->size)
        return 0;

    u64 available = file->size - file->offset;
    if (count > available)
        count = available;

    memcpy(buffer, file->data + file->offset, count);
    file->offset += count;
    return (long)count;
}

int vfs_close(int fd)
{
    if (fd < VFS_FD_BASE)
        return -1;
    int index = fd - VFS_FD_BASE;
    if (index < 0 || index >= VFS_MAX_FILES || !g_files[index].used)
        return -1;

    kfree(g_files[index].data);
    memset(&g_files[index], 0, sizeof(g_files[index]));
    return 0;
}

void vfs_close_all(void)
{
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (!g_files[i].used)
            continue;
        kfree(g_files[i].data);
        memset(&g_files[i], 0, sizeof(g_files[i]));
    }
}

int vfs_fat_mounted(void)
{
    return g_fat_mounted;
}

int vfs_data_fallback(void)
{
    if (g_fat_mounted)
        return 0;
    int is_dir = 0;
    return ramfs_stat("/data", &is_dir, NULL) == 0 && is_dir;
}

const char *vfs_fat_label(void)
{
    return g_fat_mounted ? fat32_label() : NULL;
}
