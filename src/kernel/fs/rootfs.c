/*
 * Zhaoxu OS - writable root filesystem.
 *
 * The ustar initramfs is immutable, so rootfs presents it as the lower layer
 * and a ramfs as the copy-up layer: reads fall through to the archive, the
 * first write creates a shadow file in ramfs, and unlink records a tombstone
 * that hides the lower entry.  This is the same layering model Linux uses
 * for rootfs before the real root is mounted, implemented without exposing
 * mutable archive state.
 */
#include <kprintf.h>
#include <mem.h>
#include <ramfs.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define ROOTFS_SEEN 96
#define ROOTFS_TOMBSTONES 64

struct rootfs_state {
    vfs_list_cb callback;
    void *context;
    int count;
    char names[ROOTFS_SEEN][96];
};

static struct wramfs *g_upper;
static char g_tombstones[ROOTFS_TOMBSTONES][VFS_PATH_MAX];
static int g_tombstone_count;

static int join_abs(const char *rel, char *out, usize capacity)
{
    usize length = strlen(rel ? rel : "");
    if (length + 2 > capacity)
        return -1;
    out[0] = '/';
    if (length)
        memcpy(out + 1, rel, length + 1);
    else
        out[1] = 0;
    return 0;
}

static int join_rel(const char *rel, const char *name, char *out,
                    usize capacity)
{
    usize base = strlen(rel ? rel : "");
    usize tail = strlen(name ? name : "");
    usize needed = base + (base ? 1 : 0) + tail + 1;
    if (needed > capacity)
        return -1;
    usize used = 0;
    if (base) {
        memcpy(out, rel, base);
        used = base;
        out[used++] = '/';
    }
    memcpy(out + used, name, tail + 1);
    return 0;
}

/* A tombstone hides the deleted path and everything below it, so a removed
 * lower directory cannot leak its children through a direct lookup. */
static int tombstoned(const char *rel)
{
    for (int i = 0; i < g_tombstone_count; i++) {
        const char *entry = g_tombstones[i];
        usize length = strlen(entry);
        if (strncmp(entry, rel, length) == 0 &&
            (rel[length] == 0 || rel[length] == '/'))
            return 1;
    }
    return 0;
}

static void tombstone_add(const char *rel)
{
    if (!rel || !rel[0] || tombstoned(rel))
        return;
    if (g_tombstone_count >= ROOTFS_TOMBSTONES)
        return;
    strncpy(g_tombstones[g_tombstone_count], rel, VFS_PATH_MAX - 1);
    g_tombstones[g_tombstone_count][VFS_PATH_MAX - 1] = 0;
    g_tombstone_count++;
}

static void tombstone_remove(const char *rel)
{
    for (int i = 0; i < g_tombstone_count; i++) {
        if (strcmp(g_tombstones[i], rel) != 0)
            continue;
        for (int j = i; j + 1 < g_tombstone_count; j++)
            memcpy(g_tombstones[j], g_tombstones[j + 1], VFS_PATH_MAX);
        g_tombstone_count--;
        return;
    }
}

static int rootfs_has_upper(const char *rel)
{
    int is_dir = 0;
    u64 size = 0;
    return wramfs_stat(g_upper, rel, &is_dir, &size) == 0;
}

static int rootfs_has_lower(const char *rel)
{
    char path[VFS_PATH_MAX];
    int is_dir = 0;
    u64 size = 0;
    if (join_abs(rel, path, sizeof(path)) != 0)
        return 0;
    return ramfs_stat(path, &is_dir, &size) == 0;
}

struct rootfs_list_ctx {
    struct rootfs_state *state;
    const char *rel;
    int lower;
};

static void rootfs_emit(const char *name, u64 size, int is_dir,
                        void *context)
{
    struct rootfs_list_ctx *ctx = context;
    struct rootfs_state *state = ctx->state;
    if (!name || !name[0])
        return;
    if (ctx->lower) {
        char child[VFS_PATH_MAX];
        if (join_rel(ctx->rel, name, child, sizeof(child)) == 0 &&
            tombstoned(child))
            return;
    }
    for (int i = 0; i < state->count; i++)
        if (strcmp(state->names[i], name) == 0)
            return;
    if (state->count < ROOTFS_SEEN) {
        strncpy(state->names[state->count], name,
                sizeof(state->names[0]) - 1);
        state->count++;
    }
    state->callback(name, size, is_dir, state->context);
}

static int rootfs_list(void *private, const char *rel, vfs_list_cb callback,
                       void *context)
{
    (void)private;
    struct rootfs_state *state = kzalloc(sizeof(*state));
    if (!state)
        return -1;
    state->callback = callback;
    state->context = context;

    struct rootfs_list_ctx upper = {state, rel ? rel : "", 0};
    int upper_ok = wramfs_list(g_upper, rel, rootfs_emit, &upper);

    char path[VFS_PATH_MAX];
    int lower_ok = -1;
    struct rootfs_list_ctx lower = {state, rel ? rel : "", 1};
    if (join_abs(rel, path, sizeof(path)) == 0)
        lower_ok = ramfs_list(path, rootfs_emit, &lower);

    kfree(state);
    return (upper_ok == 0 || lower_ok == 0) ? 0 : -1;
}

static int rootfs_stat(void *private, const char *rel, int *is_dir,
                       u64 *size)
{
    (void)private;
    if (tombstoned(rel ? rel : ""))
        return -1;
    if (wramfs_stat(g_upper, rel, is_dir, size) == 0)
        return 0;
    char path[VFS_PATH_MAX];
    if (join_abs(rel, path, sizeof(path)) != 0)
        return -1;
    return ramfs_stat(path, is_dir, size);
}

static int rootfs_read(void *private, const char *rel, u64 offset,
                       void *buffer, u64 capacity, u64 *read_out)
{
    (void)private;
    if (tombstoned(rel ? rel : ""))
        return -1;
    int is_dir = 0;
    u64 size = 0;
    if (wramfs_stat(g_upper, rel, &is_dir, &size) == 0)
        return wramfs_read(g_upper, rel, offset, buffer, capacity,
                           read_out);
    char path[VFS_PATH_MAX];
    if (join_abs(rel, path, sizeof(path)) != 0)
        return -1;
    return ramfs_read_at(path, offset, buffer, capacity, read_out);
}

static int rootfs_write(void *private, const char *rel, const u8 *data,
                        u64 size)
{
    (void)private;
    if (!rel || !rel[0])
        return -1;
    tombstone_remove(rel);
    return wramfs_write(g_upper, rel, data, size);
}

static int rootfs_unlink(void *private, const char *rel)
{
    (void)private;
    if (!rel || !rel[0])
        return -1;
    int removed = 0;
    if (rootfs_has_upper(rel))
        removed = wramfs_unlink(g_upper, rel) == 0;
    if (rootfs_has_lower(rel)) {
        tombstone_add(rel);
        removed = 1;
    }
    return removed ? 0 : -1;
}

static const struct vfs_fs_ops g_rootfs_ops = {
    .name = "rootfs",
    .list = rootfs_list,
    .stat = rootfs_stat,
    .read = rootfs_read,
    .write = rootfs_write,
    .unlink = rootfs_unlink,
};

/* Build the ramfs upper layer and mount the writable view at "/". */
int rootfs_mount(void)
{
    g_upper = wramfs_create();
    if (!g_upper)
        return -1;
    memset(g_tombstones, 0, sizeof(g_tombstones));
    g_tombstone_count = 0;
    if (vfs_mount("/", "rootfs", &g_rootfs_ops, NULL) != 0) {
        wramfs_destroy(g_upper);
        g_upper = NULL;
        return -1;
    }
    kprintf("[rootfs] initramfs lower + ramfs copy-up mounted at /\n");
    return 0;
}
