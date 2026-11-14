/*
 * Zhaoxu OS - writable overlay filesystem.
 *
 * The lower layer is read-only and the upper layer is a writable ramfs.  A
 * write replaces the whole file in the upper layer (the VFS write API is a
 * full-file commit), so copy-up reduces to storing the new data.  Deleting a
 * lower entry creates a ".wh.<name>" whiteout in the upper layer; listings
 * hide both whiteout markers and the lower names they cover.
 */
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define OVERLAY_SEEN 64
#define OVERLAY_NAME 96
#define WHITEOUT_PREFIX ".wh."

struct overlay {
    char lower[VFS_PATH_MAX];
    char upper[VFS_PATH_MAX];
};

static int overlay_join(const char *base, const char *rel, char *out,
                        usize capacity)
{
    if (!rel || !rel[0]) {
        usize length = strlen(base);
        if (length + 1 > capacity)
            return -1;
        memcpy(out, base, length + 1);
        return 0;
    }
    int written;
    if (strcmp(base, "/") == 0)
        written = ksnprintf(out, capacity, "/%s", rel);
    else
        written = ksnprintf(out, capacity, "%s/%s", base, rel);
    return written > 0 && (usize)written < capacity ? 0 : -1;
}

struct overlay_seen {
    vfs_list_cb callback;
    void *context;
    int count;
    int lower;
    char names[OVERLAY_SEEN][OVERLAY_NAME];
    char whiteouts[OVERLAY_SEEN][OVERLAY_NAME];
    int whiteout_count;
    const char *rel;
    const struct overlay *fs;
};

static int seen_name(struct overlay_seen *seen, const char *name)
{
    for (int i = 0; i < seen->count; i++)
        if (strcmp(seen->names[i], name) == 0)
            return 1;
    return 0;
}

static int is_whiteout(struct overlay_seen *seen, const char *name)
{
    for (int i = 0; i < seen->whiteout_count; i++)
        if (strcmp(seen->whiteouts[i], name) == 0)
            return 1;
    return 0;
}

static void overlay_emit(const char *name, u64 size, int is_dir,
                         void *context)
{
    struct overlay_seen *seen = context;
    if (!name || !name[0])
        return;

    if (!seen->lower) {
        if (strncmp(name, WHITEOUT_PREFIX, 4) == 0) {
            const char *hidden = name + 4;
            if (seen->whiteout_count < OVERLAY_SEEN)
                strncpy(seen->whiteouts[seen->whiteout_count++], hidden,
                        OVERLAY_NAME - 1);
            return;
        }
    } else if (is_whiteout(seen, name)) {
        return;
    }
    if (seen_name(seen, name))
        return;
    if (seen->count < OVERLAY_SEEN) {
        strncpy(seen->names[seen->count], name, OVERLAY_NAME - 1);
        seen->count++;
    }
    seen->callback(name, size, is_dir, seen->context);
}

static int overlay_list(void *private, const char *rel, vfs_list_cb callback,
                        void *context)
{
    struct overlay *overlay = private;
    if (!overlay)
        return -1;
    char path[VFS_PATH_MAX];
    struct overlay_seen seen;
    memset(&seen, 0, sizeof(seen));
    seen.callback = callback;
    seen.context = context;
    seen.rel = rel ? rel : "";
    seen.fs = overlay;

    seen.lower = 0;
    int upper_ok = -1;
    if (overlay_join(overlay->upper, rel, path, sizeof(path)) == 0)
        upper_ok = vfs_list(path, overlay_emit, &seen);

    seen.lower = 1;
    int lower_ok = -1;
    if (overlay_join(overlay->lower, rel, path, sizeof(path)) == 0)
        lower_ok = vfs_list(path, overlay_emit, &seen);

    return (upper_ok == 0 || lower_ok == 0) ? 0 : -1;
}

static int overlay_is_whiteout(struct overlay *overlay, const char *rel)
{
    if (!rel || !rel[0])
        return 0;
    const char *slash = strrchr(rel, '/');
    const char *name = slash ? slash + 1 : rel;
    char parent[VFS_PATH_MAX];
    if (slash) {
        usize length = (usize)(slash - rel);
        if (length >= sizeof(parent))
            return 0;
        memcpy(parent, rel, length);
        parent[length] = 0;
    } else {
        parent[0] = 0;
    }

    char path[VFS_PATH_MAX];
    if (overlay_join(overlay->upper, parent, path, sizeof(path)) != 0)
        return 0;
    char marker[VFS_PATH_MAX];
    int written;
    if (strcmp(path, "/") == 0)
        written = ksnprintf(marker, sizeof(marker), "/%s%s",
                            WHITEOUT_PREFIX, name);
    else
        written = ksnprintf(marker, sizeof(marker), "%s/%s%s", path,
                            WHITEOUT_PREFIX, name);
    if (written <= 0 || (usize)written >= sizeof(marker))
        return 0;
    int is_dir = 0;
    u64 size = 0;
    return vfs_stat(marker, &is_dir, &size) == 0;
}

static int overlay_stat(void *private, const char *rel, int *is_dir,
                        u64 *size)
{
    struct overlay *overlay = private;
    char path[VFS_PATH_MAX];
    if (!overlay)
        return -1;
    if (overlay_is_whiteout(overlay, rel))
        return -1;
    if (overlay_join(overlay->upper, rel, path, sizeof(path)) == 0 &&
        vfs_stat(path, is_dir, size) == 0)
        return 0;
    if (overlay_join(overlay->lower, rel, path, sizeof(path)) == 0 &&
        vfs_stat(path, is_dir, size) == 0)
        return 0;
    return -1;
}

static int overlay_read(void *private, const char *rel, u64 offset,
                        void *buffer, u64 capacity, u64 *read_out)
{
    struct overlay *overlay = private;
    char path[VFS_PATH_MAX];
    if (!overlay)
        return -1;
    if (overlay_is_whiteout(overlay, rel))
        return -1;
    if (overlay_join(overlay->upper, rel, path, sizeof(path)) == 0 &&
        vfs_read_at(path, offset, buffer, capacity, read_out) == 0)
        return 0;
    if (overlay_join(overlay->lower, rel, path, sizeof(path)) == 0 &&
        vfs_read_at(path, offset, buffer, capacity, read_out) == 0)
        return 0;
    return -1;
}

static int overlay_write(void *private, const char *rel, const u8 *data,
                         u64 size)
{
    struct overlay *overlay = private;
    char path[VFS_PATH_MAX];
    if (!overlay || !rel || !rel[0] ||
        overlay_join(overlay->upper, rel, path, sizeof(path)) != 0)
        return -1;
    /* Writing a whiteout marker would hide the new file. */
    int result = vfs_write_file(path, data, size);
    if (result != 0)
        return -1;

    const char *slash = strrchr(rel, '/');
    char marker[VFS_PATH_MAX];
    int written;
    if (slash)
        written = ksnprintf(marker, sizeof(marker), "%.*s/%s%s",
                            (int)(slash - rel), rel, WHITEOUT_PREFIX,
                            slash + 1);
    else
        written = ksnprintf(marker, sizeof(marker), "%s%s",
                            WHITEOUT_PREFIX, rel);
    if (written > 0 && (usize)written < sizeof(marker)) {
        char upper_marker[VFS_PATH_MAX];
        if (overlay_join(overlay->upper, marker, upper_marker,
                         sizeof(upper_marker)) == 0)
            vfs_unlink(upper_marker);
    }
    return 0;
}

static int overlay_unlink(void *private, const char *rel)
{
    struct overlay *overlay = private;
    char path[VFS_PATH_MAX];
    if (!overlay || !rel || !rel[0])
        return -1;
    int removed = 0;
    if (overlay_join(overlay->upper, rel, path, sizeof(path)) == 0 &&
        vfs_stat(path, NULL, NULL) == 0) {
        removed = vfs_unlink(path) == 0;
    }

    const char *slash = strrchr(rel, '/');
    char marker_rel[VFS_PATH_MAX];
    int written;
    if (slash)
        written = ksnprintf(marker_rel, sizeof(marker_rel), "%.*s/%s%s",
                            (int)(slash - rel), rel, WHITEOUT_PREFIX,
                            slash + 1);
    else
        written = ksnprintf(marker_rel, sizeof(marker_rel), "%s%s",
                            WHITEOUT_PREFIX, rel);
    if (written > 0 && (usize)written < sizeof(marker_rel)) {
        char marker[VFS_PATH_MAX];
        if (overlay_join(overlay->upper, marker_rel, marker,
                         sizeof(marker)) == 0) {
            static const u8 empty = 0;
            if (vfs_write_file(marker, &empty, 0) == 0)
                removed = 1;
        }
    }
    return removed ? 0 : -1;
}

static const struct vfs_fs_ops g_overlay_ops = {
    .name = "overlayfs",
    .list = overlay_list,
    .stat = overlay_stat,
    .read = overlay_read,
    .write = overlay_write,
    .unlink = overlay_unlink,
};

int overlayfs_mount(void)
{
    struct overlay *overlay = kzalloc(sizeof(*overlay));
    if (!overlay)
        return -1;
    strcpy(overlay->lower, "/");
    strcpy(overlay->upper, "/ram/upper");
    if (vfs_mount("/merged", "overlayfs", &g_overlay_ops, overlay) != 0) {
        kfree(overlay);
        return -1;
    }
    kprintf("[overlay] lower / upper /ram/upper mounted at /merged\n");
    return 0;
}
