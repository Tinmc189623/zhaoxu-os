/*
 * Zhaoxu OS - tmpfs / ramfs.
 *
 * A small writable in-memory filesystem.  Every mount owns an independent
 * node pool so a mount can be dropped without touching another instance.
 * The pool and byte limits are deliberate: the kernel heap is a shared,
 * finite resource and a runaway /tmp write must not starve the VFS.
 */
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define TMPFS_NAME_MAX 64
#define TMPFS_MAX_NODES 256
#define TMPFS_MAX_FILE (4u * 1024u * 1024u)
#define TMPFS_MAX_TOTAL (8u * 1024u * 1024u)

struct tmpfs_node {
    int used;
    int is_dir;
    char name[TMPFS_NAME_MAX];
    u8 *data;
    u64 size;
    struct tmpfs_node *parent;
    struct tmpfs_node *children;
    struct tmpfs_node *next;
};

struct tmpfs {
    struct tmpfs_node nodes[TMPFS_MAX_NODES];
    struct tmpfs_node *root;
    u64 used_bytes;
};

static struct tmpfs_node *tmpfs_child(struct tmpfs_node *directory,
                                      const char *name)
{
    if (!directory || !directory->is_dir)
        return NULL;
    for (struct tmpfs_node *child = directory->children; child;
         child = child->next) {
        if (strcmp(child->name, name) == 0)
            return child;
    }
    return NULL;
}

static struct tmpfs_node *tmpfs_alloc_node(struct tmpfs *fs,
                                           struct tmpfs_node *parent,
                                           const char *name, int is_dir)
{
    for (int i = 0; i < TMPFS_MAX_NODES; i++) {
        struct tmpfs_node *node = &fs->nodes[i];
        if (node->used)
            continue;
        memset(node, 0, sizeof(*node));
        node->used = 1;
        node->is_dir = is_dir;
        strncpy(node->name, name, sizeof(node->name) - 1);
        node->parent = parent;
        node->next = parent->children;
        parent->children = node;
        return node;
    }
    return NULL;
}

static struct tmpfs_node *tmpfs_lookup(struct tmpfs *fs, const char *rel)
{
    struct tmpfs_node *node = fs->root;
    const char *cursor = rel;
    while (cursor && *cursor) {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;

        char component[TMPFS_NAME_MAX];
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 < sizeof(component))
                component[length] = *cursor;
            length++;
            cursor++;
        }
        if (length == 0 || length >= sizeof(component) ||
            strcmp(component, ".") == 0 || strcmp(component, "..") == 0)
            return NULL;
        component[length] = 0;
        node = tmpfs_child(node, component);
        if (!node)
            return NULL;
    }
    return node;
}

static struct tmpfs_node *tmpfs_create(struct tmpfs *fs, const char *rel,
                                       int is_dir)
{
    struct tmpfs_node *node = fs->root;
    const char *cursor = rel;
    while (cursor && *cursor) {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;

        char component[TMPFS_NAME_MAX];
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 < sizeof(component))
                component[length] = *cursor;
            length++;
            cursor++;
        }
        if (length == 0 || length >= sizeof(component) ||
            strcmp(component, ".") == 0 || strcmp(component, "..") == 0)
            return NULL;
        component[length] = 0;

        int last = 1;
        const char *peek = cursor;
        while (*peek == '/')
            peek++;
        if (*peek)
            last = 0;

        struct tmpfs_node *child = tmpfs_child(node, component);
        if (!child) {
            child = tmpfs_alloc_node(fs, node, component,
                                     last ? is_dir : 1);
            if (!child)
                return NULL;
        } else if (!last && !child->is_dir) {
            return NULL;
        } else if (last && child->is_dir != is_dir) {
            return NULL;
        }

        if (last)
            return child;
        node = child;
    }
    return node;
}

static void tmpfs_unlink_node(struct tmpfs *fs, struct tmpfs_node *node)
{
    if (!node || node == fs->root || !node->parent)
        return;
    struct tmpfs_node **link = &node->parent->children;
    while (*link && *link != node)
        link = &(*link)->next;
    if (*link != node)
        return;
    *link = node->next;
    if (node->data)
        fs->used_bytes -= node->size;
    kfree(node->data);
    memset(node, 0, sizeof(*node));
}

static int tmpfs_list(void *private, const char *rel,
                      vfs_list_cb callback, void *context)
{
    struct tmpfs *fs = private;
    struct tmpfs_node *node = tmpfs_lookup(fs, rel);
    if (!node || !node->is_dir)
        return -1;
    for (struct tmpfs_node *child = node->children; child;
         child = child->next) {
        callback(child->name, child->size, child->is_dir, context);
    }
    return 0;
}

static int tmpfs_stat(void *private, const char *rel, int *is_dir,
                      u64 *size)
{
    struct tmpfs *fs = private;
    struct tmpfs_node *node = tmpfs_lookup(fs, rel);
    if (!node)
        return -1;
    if (is_dir)
        *is_dir = node->is_dir;
    if (size)
        *size = node->size;
    return 0;
}

static int tmpfs_read(void *private, const char *rel, u64 offset,
                      void *buffer, u64 capacity, u64 *read_out)
{
    struct tmpfs *fs = private;
    struct tmpfs_node *node = tmpfs_lookup(fs, rel);
    if (!node || node->is_dir || (!buffer && capacity))
        return -1;
    if (read_out)
        *read_out = 0;
    if (offset >= node->size)
        return 0;

    u64 available = node->size - offset;
    u64 count = capacity < available ? capacity : available;
    if (count)
        memcpy(buffer, node->data + offset, (usize)count);
    if (read_out)
        *read_out = count;
    return 0;
}

static int tmpfs_write(void *private, const char *rel, const u8 *data,
                       u64 size)
{
    struct tmpfs *fs = private;
    if (!rel || !rel[0] || (size && !data) || size > TMPFS_MAX_FILE)
        return -1;

    struct tmpfs_node *node = tmpfs_create(fs, rel, 0);
    if (!node || node->is_dir)
        return -1;
    if (fs->used_bytes - node->size + size > TMPFS_MAX_TOTAL)
        return -1;

    u8 *copy = NULL;
    if (size) {
        copy = kmalloc((usize)size);
        if (!copy)
            return -1;
        memcpy(copy, data, (usize)size);
    }
    if (node->data)
        fs->used_bytes -= node->size;
    kfree(node->data);
    node->data = copy;
    node->size = size;
    if (copy)
        fs->used_bytes += size;
    return 0;
}

static int tmpfs_unlink(void *private, const char *rel)
{
    struct tmpfs *fs = private;
    struct tmpfs_node *node = tmpfs_lookup(fs, rel);
    if (!node || node == fs->root)
        return -1;
    if (node->is_dir && node->children)
        return -1;
    tmpfs_unlink_node(fs, node);
    return 0;
}

static const struct vfs_fs_ops g_tmpfs_ops = {
    .name = "tmpfs",
    .list = tmpfs_list,
    .stat = tmpfs_stat,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .unlink = tmpfs_unlink,
};

int tmpfs_mount(const char *path, const char *type)
{
    struct tmpfs *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return -1;
    fs->root = &fs->nodes[0];
    fs->root->used = 1;
    fs->root->is_dir = 1;
    strcpy(fs->root->name, "/");
    fs->used_bytes = 0;

    if (vfs_mount(path, type ? type : "tmpfs", &g_tmpfs_ops, fs) != 0) {
        kfree(fs);
        return -1;
    }
    return 0;
}
