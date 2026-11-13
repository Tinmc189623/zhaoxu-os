/*
 * Zhaoxu OS - virtual file system
 *
 * The initramfs ramfs provides the root filesystem.  /mnt is served by the
 * Ring3 ext4 service (/bin/ext4fs) through the shared-page RPC in fs_rpc.h;
 * the kernel owns no ext4 code at all.  A FAT32 volume is still probed when
 * the service has no ext4 volume, so existing ZHAOXUDATA data disks keep
 * working as a compatibility fallback.  When neither backend is present,
 * /mnt falls back to the embedded copy of the data volume under /data in the
 * ramfs, so ISO-only and disk-less boots still expose the same files.
 *
 * File descriptors are read-only.  ramfs and FAT32 files are read into memory
 * when opened; ext4 files are streamed in FS_DATA_SIZE chunks straight into
 * the caller's buffer, so opening a multi-megabyte font costs no kernel heap.
 * The RPC data page is only valid inside a call, so every read hands its final
 * destination to fs_call_read/fs_call_list as the receive buffer instead of
 * copying out of fs_data() afterwards.
 */
#include <fat32.h>
#include <fs_rpc.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <thread.h>
#include <types.h>
#include <vfs.h>

#define VFS_MAX_FILES 8
#define VFS_FD_BASE 3
/* Upper bound for a whole-file read (vfs_read_all).  Anything larger is a
 * caller bug: the only whole-file consumers are executables and fonts. */
#define VFS_READ_ALL_MAX (16u * 1024u * 1024u)

/* One record of an FS_OP_LIST reply, as written by the ext4 service. */
struct list_item {
    u8 name_len;
    u8 is_dir;
    u16 pad;
    u64 size;
    char name[];
};

struct vfs_file {
    int used;
    int kind; /* 0 = buffer, 1 = ext4 stream */
    u8 *data;
    u64 size;
    u64 offset;
    char path[256];
};

static struct vfs_file g_files[VFS_MAX_FILES];
static int g_data_fs; /* 0 = none, 1 = FAT32, 2 = ext4 */

/*
 * The ext4 service keeps one write session in globals (expected offset, arena,
 * total size), so two processes streaming chunks of different files would
 * corrupt each other.  Every whole-file writer holds this latch for its chunk
 * loop; readers need no latch because each RPC is already serialised by the
 * RPC busy flag.  Wait with the same 1 ms sleep the RPC layer uses.
 */
static volatile int g_write_owner;

static void write_lock(void)
{
    while (__atomic_test_and_set(&g_write_owner, __ATOMIC_ACQUIRE))
        thread_sleep_ms(1);
}

static void write_unlock(void)
{
    __atomic_clear(&g_write_owner, __ATOMIC_RELEASE);
}

static int path_is_mnt(const char *path)
{
    if (!path)
        return 0;
    if (strncmp(path, "/mnt", 4) != 0)
        return 0;
    return path[4] == 0 || path[4] == '/';
}

/* Path inside the ext4 volume: "/mnt" -> "/", "/mnt/a" -> "/a". */
static const char *mnt_inner(const char *path)
{
    return path[4] ? path + 4 : "/";
}

/*
 * With no mounted volume, translate /mnt/... to the embedded mirror at
 * /data/....  Returns 1 when the translation was written to buffer.
 */
static int mnt_fallback_path(const char *path, char *buffer, usize capacity)
{
    if (g_data_fs || !path_is_mnt(path))
        return 0;
    int written = ksnprintf(buffer, capacity, "/data%s", path + 4);
    return written > 0 && (usize)written < capacity;
}

/* FS_OP_LIST, using rpc->offset as the entry cursor and handing each record
 * to the caller's callback.  The reply page is a private buffer: the shared
 * data page is only owned by this request inside the RPC busy window. */
static int mnt_list(const char *path, vfs_list_cb callback, void *context)
{
    u8 *page = kmalloc(FS_DATA_SIZE);
    if (!page)
        return -1;

    /* `count` comes from the service, so every record is bounds checked
     * against the bytes actually written before it is dereferenced. */
    int result = -1;
    u32 cursor = 0;
    for (;;) {
        u32 written = 0;
        u32 next = 0;
        u32 count = 0;
        if (fs_call_list(path, cursor, page, FS_DATA_SIZE, &written, &next,
                         &count) != 0)
            goto out;

        u32 used = written < FS_DATA_SIZE ? written : FS_DATA_SIZE;
        u8 *end = page + used;
        u8 *p = page;
        for (u32 i = 0; i < count; i++) {
            if (p + 16 > end)
                goto out; /* malformed reply: stop instead of guessing */
            struct list_item *item = (struct list_item *)p;
            if ((u8 *)item->name + item->name_len + 1u > end)
                goto out;
            u32 stride = (16u + item->name_len + 1u + 15u) & ~15u;
            if (!stride || p + stride > end)
                goto out;
            callback(item->name, item->size, item->is_dir != 0, context);
            p += stride;
        }
        if (!count || next <= cursor)
            break;
        cursor = next;
    }
    result = 0;

out:
    kfree(page);
    return result;
}

int vfs_init(void)
{
    memset(g_files, 0, sizeof(g_files));
    g_data_fs = 0;
    return ramfs_init();
}

int vfs_mount_data(void)
{
    if (fs_service_ready() && fs_call_mount() == 0) {
        g_data_fs = 2;
        return 0;
    }
    if (fat32_mount() == 0) {
        g_data_fs = 1;
        return 0;
    }
    return -1;
}

int vfs_list(const char *path, vfs_list_cb callback, void *context)
{
    if (!path || !*path)
        path = "/";

    char alt[300];
    if (mnt_fallback_path(path, alt, sizeof(alt)))
        return ramfs_list(alt, callback, context);
    if (g_data_fs && path_is_mnt(path)) {
        const char *inner = mnt_inner(path);
        return g_data_fs == 2 ? mnt_list(inner, callback, context)
                              : fat32_list(inner, callback, context);
    }
    return ramfs_list(path, callback, context);
}

int vfs_read_all(const char *path, u8 **buffer, u64 *size)
{
    if (!path || !buffer || !size)
        return -1;
    char alt[300];
    if (mnt_fallback_path(path, alt, sizeof(alt)))
        return ramfs_read_all(alt, buffer, size);
    if (g_data_fs == 1 && path_is_mnt(path))
        return fat32_read_all(mnt_inner(path), buffer, size);
    if (g_data_fs == 2 && path_is_mnt(path)) {
        int is_dir = 0;
        u64 total = 0;
        if (fs_call_stat(mnt_inner(path), &is_dir, &total) != 0 || is_dir)
            return -1;
        if (total > VFS_READ_ALL_MAX)
            return -1;

        u8 *data = kmalloc(total ? (usize)total : 1);
        if (!data)
            return -1;

        u64 offset = 0;
        while (offset < total) {
            u64 remaining = total - offset;
            u32 chunk = remaining > FS_DATA_SIZE ? FS_DATA_SIZE
                                                 : (u32)remaining;
            u32 got = 0;
            if (fs_call_read(mnt_inner(path), offset, data + offset, chunk,
                             &got) != 0 ||
                !got)
                break;
            offset += got;
            if (got < chunk)
                break;
        }
        if (offset != total) {
            kfree(data);
            return -1;
        }
        *buffer = data;
        *size = total;
        return 0;
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
    if (g_data_fs == 1 && path_is_mnt(path))
        return fat32_read_into(mnt_inner(path), dst, cap, read_out);
    if (g_data_fs == 2 && path_is_mnt(path)) {
        if (!dst && cap)
            return -1;
        u64 offset = 0;
        while (offset < cap) {
            u64 remaining = cap - offset;
            u32 chunk = remaining > FS_DATA_SIZE ? FS_DATA_SIZE
                                                 : (u32)remaining;
            u32 got = 0;
            if (fs_call_read(mnt_inner(path), offset, dst + offset, chunk,
                             &got) != 0)
                return -1;
            if (!got)
                break;
            offset += got;
            if (got < chunk)
                break;
        }
        if (read_out)
            *read_out = offset;
        return 0;
    }
    return ramfs_read_into(path, dst, cap, read_out);
}

int vfs_write_file(const char *path, const u8 *data, u64 size)
{
    if (!path || !g_data_fs)
        return -1;
    if (!path_is_mnt(path) || !path[4])
        return -1;
    /* The service holds the whole file in one 8 MiB arena. */
    if (size > VFS_WRITE_MAX)
        return -1;
    if (g_data_fs == 1)
        return fat32_write_file(path + 4, data, size);

    /* The service commits the file when the last chunk arrives, so the chunks
     * must be sent in ascending order with the total size as the stop mark.
     * The latch keeps another writer's chunks from entering the session. */
    write_lock();
    int result = -1;
    u64 offset = 0;
    for (;;) {
        u64 remaining = size - offset;
        u32 chunk = remaining > FS_DATA_SIZE ? FS_DATA_SIZE : (u32)remaining;
        if (fs_call_write(path + 4, size, offset, chunk ? data + offset : 0,
                          chunk, offset == 0) != 0)
            goto out;
        offset += chunk;
        if (offset >= size)
            break;
    }
    result = 0;

out:
    write_unlock();
    return result;
}

int vfs_unlink(const char *path)
{
    if (!path || !g_data_fs)
        return -1;
    if (!path_is_mnt(path) || !path[4])
        return -1;
    if (g_data_fs == 1)
        return fat32_unlink(path + 4);

    /* An unlink between two chunks of a streamed write would break the
     * session, so it waits for the writer like another writer would. */
    write_lock();
    int result = fs_call_unlink(path + 4);
    write_unlock();
    return result;
}

int vfs_stat(const char *path, int *is_dir, u64 *size)
{
    if (!path)
        return -1;
    char alt[300];
    if (mnt_fallback_path(path, alt, sizeof(alt)))
        return ramfs_stat(alt, is_dir, size);
    if (g_data_fs && path_is_mnt(path)) {
        const char *inner = mnt_inner(path);
        return g_data_fs == 2 ? fs_call_stat(inner, is_dir, size)
                              : fat32_stat(inner, is_dir, size);
    }
    return ramfs_stat(path, is_dir, size);
}

int vfs_open(const char *path)
{
    if (!path)
        return -1;

    /* Files on the ext4 volume stream; every other path (/etc/motd,
     * /bin/...) falls through to the ramfs read below. */
    if (g_data_fs == 2 && path_is_mnt(path)) {
        int is_dir = 0;
        u64 size = 0;
        if (!path[4])
            return -1; /* /mnt itself is a directory */
        if (fs_call_stat(path + 4, &is_dir, &size) != 0 || is_dir)
            return -1;
        for (int i = 0; i < VFS_MAX_FILES; i++) {
            if (g_files[i].used)
                continue;
            g_files[i].used = 1;
            g_files[i].kind = 1;
            g_files[i].data = 0;
            g_files[i].size = size;
            g_files[i].offset = 0;
            /* Store the path relative to the ext4 root ("/mnt/a" -> "/a"):
             * every later FS_OP_READ is resolved inside the volume. */
            const char *inner = path + 4;
            usize n;
            for (n = 0; inner[n] && n + 1 < sizeof(g_files[i].path); n++)
                g_files[i].path[n] = inner[n];
            g_files[i].path[n] = 0;
            return i + VFS_FD_BASE;
        }
        return -1;
    }

    u8 *data = NULL;
    u64 size = 0;
    if (vfs_read_all(path, &data, &size) != 0)
        return -1;

    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (g_files[i].used)
            continue;
        g_files[i].used = 1;
        g_files[i].kind = 0;
        g_files[i].data = data;
        g_files[i].size = size;
        g_files[i].offset = 0;
        g_files[i].path[0] = 0;
        return i + VFS_FD_BASE;
    }

    kfree(data);
    return -1;
}

/* Stream an ext4 file straight into the caller's buffer.  fs_call_read copies
 * the reply there before releasing the RPC, so no bytes are read from the
 * shared data page after the call returns. */
static long mnt_read(struct vfs_file *file, u8 *buffer, u64 count)
{
    long total = 0;
    while (count) {
        u32 chunk = count > FS_DATA_SIZE ? FS_DATA_SIZE : (u32)count;
        u32 got = 0;
        if (fs_call_read(file->path, file->offset, buffer, chunk, &got) != 0)
            return -1;
        if (!got)
            break;
        file->offset += got;
        buffer += got;
        total += got;
        count -= got;
        if (got < chunk)
            break;
    }
    return total;
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
    if (file->kind == 1)
        return mnt_read(file, buffer, count);

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

    if (g_files[index].kind == 0)
        kfree(g_files[index].data);
    memset(&g_files[index], 0, sizeof(g_files[index]));
    return 0;
}

void vfs_close_all(void)
{
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (!g_files[i].used)
            continue;
        if (g_files[i].kind == 0)
            kfree(g_files[i].data);
        memset(&g_files[i], 0, sizeof(g_files[i]));
    }
}

int vfs_data_mounted(void)
{
    return g_data_fs != 0;
}

const char *vfs_data_fs(void)
{
    if (g_data_fs == 2)
        return "ext4";
    if (g_data_fs == 1)
        return "FAT32";
    return 0;
}

int vfs_data_fallback(void)
{
    if (g_data_fs)
        return 0;
    int is_dir = 0;
    return ramfs_stat("/data", &is_dir, NULL) == 0 && is_dir;
}

const char *vfs_data_label(void)
{
    if (g_data_fs == 2)
        return fs_label();
    if (g_data_fs == 1)
        return fat32_label();
    return 0;
}
