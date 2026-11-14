/*
 * Zhaoxu OS - /mnt storage adapter.
 *
 * ext4 lives in the Ring3 /bin/ext4fs service, while FAT32 remains a kernel
 * compatibility reader.  Both expose path-based APIs, so this file adapts
 * them to the VFS callback interface.  Keeping the adapter separate stops
 * the VFS core from depending on the storage services.
 */
#include <fat32.h>
#include <fs_rpc.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <thread.h>
#include <types.h>
#include <vfs.h>

#define MNT_EXT4 1
#define MNT_FAT  2

struct mnt_private {
    int kind;
    char label[32];
};

struct list_item {
    u8 name_len;
    u8 is_dir;
    u16 pad;
    u64 size;
    char name[];
};

static struct mnt_private g_ext4;
static struct mnt_private g_fat;
static int g_mounted_kind;
static char g_mounted_label[32];
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

/* ext4 and FAT32 both expect absolute paths inside their volume. */
static int mnt_path(const char *rel, char *path, usize capacity)
{
    if (!rel || !rel[0]) {
        if (capacity < 2)
            return -1;
        strcpy(path, "/");
        return 0;
    }
    usize length = strlen(rel);
    if (length + 2 > capacity)
        return -1;
    path[0] = '/';
    memcpy(path + 1, rel, length + 1);
    return 0;
}

static int ext4_list_path(const char *path, vfs_list_cb callback,
                          void *context)
{
    u8 *page = kmalloc(FS_DATA_SIZE);
    if (!page)
        return -1;

    int result = -1;
    u32 cursor = 0;
    for (;;) {
        u32 written = 0;
        u32 next = 0;
        u32 count = 0;
        if (fs_call_list(path, cursor, page, FS_DATA_SIZE, &written,
                         &next, &count) != 0)
            goto out;

        u32 used = written < FS_DATA_SIZE ? written : FS_DATA_SIZE;
        u8 *end = page + used;
        u8 *p = page;
        for (u32 i = 0; i < count; i++) {
            if (p + 16 > end)
                goto out;
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

static int mntfs_list(void *private, const char *rel,
                      vfs_list_cb callback, void *context)
{
    char path[VFS_PATH_MAX];
    struct mnt_private *mnt = private;
    if (!mnt || mnt_path(rel, path, sizeof(path)) != 0)
        return -1;
    if (mnt->kind == MNT_EXT4)
        return ext4_list_path(path, callback, context);
    return fat32_list(path, callback, context);
}

static int mntfs_stat(void *private, const char *rel, int *is_dir,
                      u64 *size)
{
    char path[VFS_PATH_MAX];
    struct mnt_private *mnt = private;
    if (!mnt || mnt_path(rel, path, sizeof(path)) != 0)
        return -1;
    if (mnt->kind == MNT_EXT4)
        return fs_call_stat(path, is_dir, size);
    return fat32_stat(path, is_dir, size);
}

static int mntfs_read(void *private, const char *rel, u64 offset,
                      void *buffer, u64 capacity, u64 *read_out)
{
    char path[VFS_PATH_MAX];
    struct mnt_private *mnt = private;
    if (!mnt || (!buffer && capacity) ||
        mnt_path(rel, path, sizeof(path)) != 0)
        return -1;

    if (mnt->kind == MNT_EXT4) {
        u32 chunk = capacity > FS_DATA_SIZE ? FS_DATA_SIZE
                                            : (u32)capacity;
        u32 got = 0;
        if (fs_call_read(path, offset, buffer, chunk, &got) != 0)
            return -1;
        if (read_out)
            *read_out = got;
        return 0;
    }
    return fat32_read_at(path, offset, (u8 *)buffer, capacity, read_out);
}

static int mntfs_write(void *private, const char *rel, const u8 *data,
                       u64 size)
{
    char path[VFS_PATH_MAX];
    struct mnt_private *mnt = private;
    if (!mnt || size > VFS_WRITE_MAX ||
        mnt_path(rel, path, sizeof(path)) != 0)
        return -1;
    if (mnt->kind == MNT_FAT)
        return fat32_write_file(path, data, size);

    write_lock();
    int result = -1;
    u64 offset = 0;
    for (;;) {
        u64 remaining = size - offset;
        u32 chunk = remaining > FS_DATA_SIZE ? FS_DATA_SIZE : (u32)remaining;
        if (fs_call_write(path, size, offset, chunk ? data + offset : 0,
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

static int mntfs_unlink(void *private, const char *rel)
{
    char path[VFS_PATH_MAX];
    struct mnt_private *mnt = private;
    if (!mnt || mnt_path(rel, path, sizeof(path)) != 0)
        return -1;
    if (mnt->kind == MNT_FAT)
        return fat32_unlink(path);

    write_lock();
    int result = fs_call_unlink(path);
    write_unlock();
    return result;
}

static const struct vfs_fs_ops g_ext4_ops = {
    .name = "ext4",
    .list = mntfs_list,
    .stat = mntfs_stat,
    .read = mntfs_read,
    .write = mntfs_write,
    .unlink = mntfs_unlink,
};

static const struct vfs_fs_ops g_fat_ops = {
    .name = "vfat",
    .list = mntfs_list,
    .stat = mntfs_stat,
    .read = mntfs_read,
    .write = mntfs_write,
    .unlink = mntfs_unlink,
};

int mntfs_mount_data(void)
{
    g_mounted_kind = 0;
    g_mounted_label[0] = 0;

    if (fs_service_ready() && fs_call_mount() == 0) {
        g_ext4.kind = MNT_EXT4;
        const char *label = fs_label();
        strncpy(g_ext4.label, label ? label : "ZHAOXUROOT",
                sizeof(g_ext4.label) - 1);
        if (vfs_mount("/mnt", "ext4", &g_ext4_ops, &g_ext4) != 0)
            return -1;
        g_mounted_kind = MNT_EXT4;
        strncpy(g_mounted_label, g_ext4.label,
                sizeof(g_mounted_label) - 1);
        return 0;
    }

    if (fat32_mount() == 0) {
        g_fat.kind = MNT_FAT;
        const char *label = fat32_label();
        strncpy(g_fat.label, label ? label : "ZHAOXUDATA",
                sizeof(g_fat.label) - 1);
        if (vfs_mount("/mnt", "vfat", &g_fat_ops, &g_fat) != 0)
            return -1;
        g_mounted_kind = MNT_FAT;
        strncpy(g_mounted_label, g_fat.label,
                sizeof(g_mounted_label) - 1);
        return 0;
    }
    return -1;
}

const char *mntfs_type(void)
{
    if (g_mounted_kind == MNT_EXT4)
        return "ext4";
    if (g_mounted_kind == MNT_FAT)
        return "vfat";
    return NULL;
}

const char *mntfs_label(void)
{
    return g_mounted_label[0] ? g_mounted_label : NULL;
}
