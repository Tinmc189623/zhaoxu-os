#ifndef ZHAOXU_SYNFS_H
#define ZHAOXU_SYNFS_H

#include <types.h>

/* A read-only synthetic tree.  File contents are generated on demand, so
 * procfs/sysfs/debugfs entries always reflect current kernel state. */
typedef int (*synfs_text_fn)(char *buffer, usize capacity);

struct synfs_entry {
    const char *path; /* mount-relative, no leading slash */
    int is_dir;
    synfs_text_fn text;
};

struct synfs {
    const struct synfs_entry *entries;
    usize count;
};

int synfs_mount(const char *path, const char *type,
                const struct synfs_entry *entries, usize count);

#endif
