/*
 * Zhaoxu OS - synthetic read-only filesystem.
 *
 * procfs, sysfs and debugfs all expose generated text.  This file owns the
 * shared lookup/listing logic; each filesystem only supplies a static entry
 * table whose file callbacks format current kernel state.
 */
#include "synfs.h"

#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define SYNFS_TEXT_MAX 4096

static const struct synfs_entry *synfs_find(const struct synfs *fs,
                                            const char *rel)
{
    if (!rel)
        return NULL;
    for (usize i = 0; i < fs->count; i++) {
        if (strcmp(fs->entries[i].path, rel) == 0)
            return &fs->entries[i];
    }
    return NULL;
}

/* Return 1 when `entry` is an immediate child of `rel`; *name receives the
 * basename.  The table lists every directory explicitly, so entries whose
 * remaining path still contains a slash are not emitted here. */
static int synfs_child(const char *entry_path, const char *rel,
                       const char **name)
{
    usize rel_len = strlen(rel);
    if (rel_len == 0) {
        if (strchr(entry_path, '/'))
            return 0;
        *name = entry_path;
        return 1;
    }
    if (strncmp(entry_path, rel, rel_len) != 0 ||
        entry_path[rel_len] != '/')
        return 0;
    const char *rest = entry_path + rel_len + 1;
    if (!*rest || strchr(rest, '/'))
        return 0;
    *name = rest;
    return 1;
}

static int synfs_text_size(const struct synfs_entry *entry)
{
    if (!entry || entry->is_dir || !entry->text)
        return 0;
    char text[SYNFS_TEXT_MAX];
    int length = entry->text(text, sizeof(text));
    if (length < 0)
        return 0;
    return length < SYNFS_TEXT_MAX ? length : SYNFS_TEXT_MAX;
}

static int synfs_list(void *private, const char *rel,
                      vfs_list_cb callback, void *context)
{
    const struct synfs *fs = private;
    if (!fs)
        return -1;
    for (usize i = 0; i < fs->count; i++) {
        const char *name = NULL;
        if (!synfs_child(fs->entries[i].path, rel, &name))
            continue;
        callback(name, (u64)synfs_text_size(&fs->entries[i]),
                 fs->entries[i].is_dir, context);
    }
    return 0;
}

static int synfs_stat(void *private, const char *rel, int *is_dir,
                      u64 *size)
{
    const struct synfs *fs = private;
    const struct synfs_entry *entry = synfs_find(fs, rel);
    if (!entry)
        return -1;
    if (is_dir)
        *is_dir = entry->is_dir;
    if (size)
        *size = (u64)synfs_text_size(entry);
    return 0;
}

static int synfs_read(void *private, const char *rel, u64 offset,
                      void *buffer, u64 capacity, u64 *read_out)
{
    const struct synfs *fs = private;
    const struct synfs_entry *entry = synfs_find(fs, rel);
    if (!entry || entry->is_dir || !entry->text || (!buffer && capacity))
        return -1;
    if (read_out)
        *read_out = 0;

    char text[SYNFS_TEXT_MAX];
    int length = entry->text(text, sizeof(text));
    if (length <= 0)
        return 0;
    if (length > SYNFS_TEXT_MAX)
        length = SYNFS_TEXT_MAX;
    if (offset >= (u64)length)
        return 0;
    u64 available = (u64)length - offset;
    u64 count = capacity < available ? capacity : available;
    if (count)
        memcpy(buffer, text + offset, (usize)count);
    if (read_out)
        *read_out = count;
    return 0;
}

static int synfs_write(void *private, const char *rel, const u8 *data,
                       u64 size)
{
    (void)private;
    (void)rel;
    (void)data;
    (void)size;
    return -1;
}

static int synfs_unlink(void *private, const char *rel)
{
    (void)private;
    (void)rel;
    return -1;
}

static const struct vfs_fs_ops g_synfs_ops = {
    .name = "synfs",
    .list = synfs_list,
    .stat = synfs_stat,
    .read = synfs_read,
    .write = synfs_write,
    .unlink = synfs_unlink,
};

int synfs_mount(const char *path, const char *type,
                const struct synfs_entry *entries, usize count)
{
    if (!path || !type || !entries)
        return -1;
    struct synfs *fs = kmalloc(sizeof(*fs));
    if (!fs)
        return -1;
    fs->entries = entries;
    fs->count = count;
    if (vfs_mount(path, type, &g_synfs_ops, fs) != 0) {
        kfree(fs);
        return -1;
    }
    return 0;
}
