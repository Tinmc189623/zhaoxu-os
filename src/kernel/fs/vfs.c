/*
 * Zhaoxu OS - virtual file system core.
 *
 * The VFS is a mount table plus a small path-based filesystem interface.  It
 * deliberately stays smaller than a Unix inode/dentry cache: the kernel is a
 * microkernel and the storage-heavy work lives in Ring 3.  Every mounted
 * filesystem receives a path relative to its mount point, so a filesystem
 * never needs to know where it was mounted.
 *
 * Open files keep the resolved filesystem pointer and relative path, which
 * lets reads stream through the filesystem's read callback instead of
 * preloading whole files.  The mount table is fixed-size; mounts are created
 * at boot and by the storage probe, never from an untrusted context.
 */
#include <kprintf.h>
#include <mem.h>
#include <ramfs.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

extern int mntfs_mount_data(void);
extern const char *mntfs_type(void);
extern const char *mntfs_label(void);

struct vfs_mount {
    int used;
    char path[VFS_PATH_MAX];
    char type[24];
    const struct vfs_fs_ops *ops;
    void *private;
};

struct vfs_file {
    int used;
    const struct vfs_fs_ops *ops;
    void *private;
    char rel[VFS_PATH_MAX];
    u64 size;
    u64 offset;
};

static struct vfs_mount g_mounts[VFS_MAX_MOUNTS];
static struct vfs_file g_files[VFS_MAX_FILES];
static char g_data_type[24];
static char g_data_label[32];

/* Collapse repeated slashes and drop a trailing slash.  Component names are
 * kept literal; a ".." component simply fails lookup in the filesystem rather
 * than being able to escape a mount point. */
static int normalize_path(const char *path, char *out, usize capacity)
{
    if (!path || path[0] != '/' || capacity < 2)
        return -1;
    usize used = 1;
    int last_slash = 1;
    out[0] = '/';

    for (usize i = 1; path[i]; i++) {
        char c = path[i];
        if (c == '/') {
            if (!last_slash) {
                if (used + 1 >= capacity)
                    return -1;
                out[used++] = '/';
                last_slash = 1;
            }
            continue;
        }
        if (used + 1 >= capacity)
            return -1;
        out[used++] = c;
        last_slash = 0;
    }

    while (used > 1 && out[used - 1] == '/')
        used--;
    out[used] = 0;
    return 0;
}

static int path_is_mnt(const char *path)
{
    if (!path || strncmp(path, "/mnt", 4) != 0)
        return 0;
    return path[4] == 0 || path[4] == '/';
}

static int mnt_mount_present(void)
{
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && strcmp(g_mounts[i].path, "/mnt") == 0)
            return 1;
    }
    return 0;
}

/* Find the longest mount whose path is a component prefix of `path`.  The
 * relative path is copied into `rel` so it remains valid after return.
 * With no storage volume, /mnt is the embedded mirror under /data. */
static const struct vfs_mount *resolve_path(const char *path, char *rel,
                                            usize capacity)
{
    char fallback[VFS_PATH_MAX];
    const char *source = path;
    if (!mnt_mount_present() && path_is_mnt(path)) {
        int written = ksnprintf(fallback, sizeof(fallback), "/data%s",
                                path + 4);
        if (written <= 0 || (usize)written >= sizeof(fallback))
            return NULL;
        source = fallback;
    }

    char normalized[VFS_PATH_MAX];
    if (normalize_path(source, normalized, sizeof(normalized)) != 0)
        return NULL;

    const struct vfs_mount *best = NULL;
    usize best_length = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used)
            continue;
        usize length = strlen(g_mounts[i].path);
        if (length < best_length)
            continue;
        if (strncmp(normalized, g_mounts[i].path, length) != 0)
            continue;
        if (length > 1 && normalized[length] != 0 &&
            normalized[length] != '/')
            continue;
        best = &g_mounts[i];
        best_length = length;
    }
    if (!best)
        return NULL;

    const char *tail = normalized + best_length;
    while (*tail == '/')
        tail++;
    usize length = strlen(tail);
    if (length + 1 > capacity)
        return NULL;
    memcpy(rel, tail, length + 1);
    return best;
}

int vfs_mount(const char *path, const char *type,
              const struct vfs_fs_ops *ops, void *private)
{
    char normalized[VFS_PATH_MAX];
    if (!ops || !type || !type[0])
        return -1;
    if (normalize_path(path, normalized, sizeof(normalized)) != 0)
        return -1;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used &&
            strcmp(g_mounts[i].path, normalized) == 0)
            return -1;
    }

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used)
            continue;
        memset(&g_mounts[i], 0, sizeof(g_mounts[i]));
        g_mounts[i].used = 1;
        strncpy(g_mounts[i].path, normalized,
                sizeof(g_mounts[i].path) - 1);
        strncpy(g_mounts[i].type, type, sizeof(g_mounts[i].type) - 1);
        g_mounts[i].ops = ops;
        g_mounts[i].private = private;
        return 0;
    }
    return -1;
}

int vfs_umount(const char *path)
{
    char normalized[VFS_PATH_MAX];
    if (normalize_path(path, normalized, sizeof(normalized)) != 0)
        return -1;
    if (strcmp(normalized, "/") == 0)
        return -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used ||
            strcmp(g_mounts[i].path, normalized) != 0)
            continue;
        memset(&g_mounts[i], 0, sizeof(g_mounts[i]));
        return 0;
    }
    return -1;
}

int vfs_mount_count(void)
{
    int count = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
        if (g_mounts[i].used)
            count++;
    return count;
}

static struct vfs_mount *mount_nth(int index)
{
    int seen = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used)
            continue;
        if (seen++ == index)
            return &g_mounts[i];
    }
    return NULL;
}

const char *vfs_mount_path(int index)
{
    struct vfs_mount *mount = mount_nth(index);
    return mount ? mount->path : NULL;
}

const char *vfs_mount_type(int index)
{
    struct vfs_mount *mount = mount_nth(index);
    return mount ? mount->type : NULL;
}

int vfs_mounts_text(char *buffer, usize capacity)
{
    if (!buffer || !capacity)
        return -1;
    usize used = 0;
    int count = vfs_mount_count();
    for (int i = 0; i < count && used + 1 < capacity; i++) {
        const char *path = vfs_mount_path(i);
        const char *type = vfs_mount_type(i);
        int written = ksnprintf(buffer + used, capacity - used,
                                "%s %s %s 0 0\n", type, type, path);
        if (written <= 0)
            break;
        if ((usize)written >= capacity - used) {
            used = capacity - 1;
            break;
        }
        used += (usize)written;
    }
    if (used < capacity)
        buffer[used] = 0;
    return (int)used;
}

int vfs_init(void)
{
    memset(g_mounts, 0, sizeof(g_mounts));
    memset(g_files, 0, sizeof(g_files));
    g_data_type[0] = 0;
    g_data_label[0] = 0;

    if (ramfs_init() != 0)
        return -1;
    if (rootfs_mount() != 0)
        return -1;

    tmpfs_mount("/tmp", "tmpfs");
    tmpfs_mount("/run", "tmpfs");
    ramfs_mount_at("/ram", "ramfs");
    procfs_mount();
    sysfs_mount();
    debugfs_mount();
    efivarfs_mount();
    overlayfs_mount();
    ecryptfs_mount();
    return 0;
}

int vfs_mount_data(void)
{
    if (mntfs_mount_data() != 0)
        return -1;
    const char *type = mntfs_type();
    const char *label = mntfs_label();
    strncpy(g_data_type, type ? type : "storage",
            sizeof(g_data_type) - 1);
    g_data_type[sizeof(g_data_type) - 1] = 0;
    strncpy(g_data_label, label ? label : "",
            sizeof(g_data_label) - 1);
    g_data_label[sizeof(g_data_label) - 1] = 0;
    return 0;
}

#define VFS_LIST_SEEN 16

struct list_seen {
    vfs_list_cb callback;
    void *context;
    int count;
    char names[VFS_LIST_SEEN][VFS_PATH_MAX];
};

static void list_seen_emit(const char *name, u64 size, int is_dir,
                           void *context)
{
    struct list_seen *seen = context;
    if (!name || !name[0])
        return;
    if (seen->count >= VFS_LIST_SEEN) {
        seen->callback(name, size, is_dir, seen->context);
        return;
    }
    for (int i = 0; i < seen->count; i++)
        if (strcmp(seen->names[i], name) == 0)
            return;
    strncpy(seen->names[seen->count], name,
            sizeof(seen->names[0]) - 1);
    seen->count++;
    seen->callback(name, size, is_dir, seen->context);
}

/* A mount point is a directory in its parent even though the parent
 * filesystem has no node for it.  Emit direct mount children so listing "/"
 * shows /proc, /sys, /tmp and friends. */
static int list_mount_children(const char *normalized,
                               struct list_seen *seen)
{
    int emitted = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used || strcmp(g_mounts[i].path, "/") == 0)
            continue;
        char parent[VFS_PATH_MAX];
        strncpy(parent, g_mounts[i].path, sizeof(parent) - 1);
        parent[sizeof(parent) - 1] = 0;
        char *slash = strrchr(parent, '/');
        if (!slash)
            continue;
        const char *base;
        if (slash == parent) {
            parent[1] = 0;
            base = slash + 1;
        } else {
            *slash = 0;
            base = slash + 1;
        }
        if (!base[0] || strcmp(parent, normalized) != 0)
            continue;
        list_seen_emit(base, 0, 1, seen);
        emitted = 1;
    }
    return emitted;
}

int vfs_list(const char *path, vfs_list_cb callback, void *context)
{
    char normalized[VFS_PATH_MAX];
    char rel[VFS_PATH_MAX];
    if (!callback ||
        normalize_path(path, normalized, sizeof(normalized)) != 0)
        return -1;
    const struct vfs_mount *mount =
        resolve_path(normalized, rel, sizeof(rel));
    if (!mount || !mount->ops->list)
        return -1;

    struct list_seen seen;
    memset(&seen, 0, sizeof(seen));
    seen.callback = callback;
    seen.context = context;

    int result = mount->ops->list(mount->private, rel,
                                  list_seen_emit, &seen);
    int mounts = list_mount_children(normalized, &seen);
    return (result == 0 || mounts) ? 0 : -1;
}

int vfs_stat(const char *path, int *is_dir, u64 *size)
{
    char rel[VFS_PATH_MAX];
    const struct vfs_mount *mount = resolve_path(path, rel, sizeof(rel));
    if (!mount || !mount->ops->stat)
        return -1;
    return mount->ops->stat(mount->private, rel, is_dir, size);
}

int vfs_read_all(const char *path, u8 **buffer, u64 *size)
{
    if (!buffer || !size)
        return -1;
    int is_dir = 0;
    u64 total = 0;
    if (vfs_stat(path, &is_dir, &total) != 0 || is_dir)
        return -1;
    if (total > 16u * 1024u * 1024u)
        return -1;

    u8 *data = kmalloc(total ? (usize)total : 1);
    if (!data)
        return -1;

    char rel[VFS_PATH_MAX];
    const struct vfs_mount *mount = resolve_path(path, rel, sizeof(rel));
    if (!mount || !mount->ops->read) {
        kfree(data);
        return -1;
    }

    u64 offset = 0;
    while (offset < total) {
        u64 got = 0;
        if (mount->ops->read(mount->private, rel, offset, data + offset,
                             total - offset, &got) != 0 || !got)
            break;
        offset += got;
    }
    if (offset != total) {
        kfree(data);
        return -1;
    }
    *buffer = data;
    *size = total;
    return 0;
}

int vfs_read_at(const char *path, u64 offset, u8 *dst, u64 cap,
                u64 *read_out)
{
    if (!dst && cap)
        return -1;
    char rel[VFS_PATH_MAX];
    const struct vfs_mount *mount = resolve_path(path, rel, sizeof(rel));
    if (!mount || !mount->ops->read)
        return -1;
    u64 got = 0;
    if (mount->ops->read(mount->private, rel, offset, dst, cap,
                         &got) != 0)
        return -1;
    if (read_out)
        *read_out = got;
    return 0;
}

int vfs_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out)
{
    if (!dst && cap)
        return -1;
    char rel[VFS_PATH_MAX];
    const struct vfs_mount *mount = resolve_path(path, rel, sizeof(rel));
    if (!mount || !mount->ops->read)
        return -1;

    u64 offset = 0;
    while (offset < cap) {
        u64 got = 0;
        if (mount->ops->read(mount->private, rel, offset, dst + offset,
                             cap - offset, &got) != 0)
            return -1;
        if (!got)
            break;
        offset += got;
    }
    if (read_out)
        *read_out = offset;
    return 0;
}

int vfs_write_file(const char *path, const u8 *data, u64 size)
{
    char rel[VFS_PATH_MAX];
    const struct vfs_mount *mount = resolve_path(path, rel, sizeof(rel));
    if (!mount || !mount->ops->write || !rel[0])
        return -1;
    return mount->ops->write(mount->private, rel, data, size);
}

int vfs_unlink(const char *path)
{
    char rel[VFS_PATH_MAX];
    const struct vfs_mount *mount = resolve_path(path, rel, sizeof(rel));
    if (!mount || !mount->ops->unlink || !rel[0])
        return -1;
    return mount->ops->unlink(mount->private, rel);
}

int vfs_open(const char *path)
{
    char rel[VFS_PATH_MAX];
    const struct vfs_mount *mount = resolve_path(path, rel, sizeof(rel));
    if (!mount || !mount->ops->read || !mount->ops->stat)
        return -1;

    int is_dir = 0;
    u64 size = 0;
    if (mount->ops->stat(mount->private, rel, &is_dir, &size) != 0 ||
        is_dir)
        return -1;

    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (g_files[i].used)
            continue;
        memset(&g_files[i], 0, sizeof(g_files[i]));
        g_files[i].used = 1;
        g_files[i].ops = mount->ops;
        g_files[i].private = mount->private;
        strncpy(g_files[i].rel, rel, sizeof(g_files[i].rel) - 1);
        g_files[i].size = size;
        g_files[i].offset = 0;
        return i + VFS_FD_BASE;
    }
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
    if (!file->ops || !file->ops->read)
        return -1;
    if (file->offset >= file->size)
        return 0;
    if (count > file->size - file->offset)
        count = file->size - file->offset;

    u64 got = 0;
    if (file->ops->read(file->private, file->rel, file->offset, buffer,
                        count, &got) != 0)
        return -1;
    file->offset += got;
    return (long)got;
}

int vfs_close(int fd)
{
    if (fd < VFS_FD_BASE)
        return -1;
    int index = fd - VFS_FD_BASE;
    if (index < 0 || index >= VFS_MAX_FILES || !g_files[index].used)
        return -1;
    memset(&g_files[index], 0, sizeof(g_files[index]));
    return 0;
}

void vfs_close_all(void)
{
    memset(g_files, 0, sizeof(g_files));
}

int vfs_data_mounted(void)
{
    return g_data_type[0] != 0;
}

const char *vfs_data_fs(void)
{
    return g_data_type[0] ? g_data_type : NULL;
}

int vfs_data_fallback(void)
{
    if (g_data_type[0])
        return 0;
    int is_dir = 0;
    return vfs_stat("/data", &is_dir, NULL) == 0 && is_dir;
}

const char *vfs_data_label(void)
{
    return g_data_label[0] ? g_data_label : NULL;
}
