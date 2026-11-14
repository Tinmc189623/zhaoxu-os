/*
 * Zhaoxu OS - writable ramfs.
 *
 * Every node and file body is individually heap allocated, so a growing
 * directory never invalidates pointers held by an in-progress lookup (a
 * fixed pool would).  File and total byte caps keep a runaway writer from
 * starving the shared kernel heap.  rootfs reuses this tree as its copy-up
 * layer.
 */
#include <kprintf.h>
#include <mem.h>
#include <ramfs.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

#define WRAMFS_NAME 96
#define WRAMFS_FILE_MAX (16u * 1024u * 1024u)
#define WRAMFS_TOTAL_MAX (64u * 1024u * 1024u)

struct wramfs_node {
    int is_dir;
    char name[WRAMFS_NAME];
    u8 *data;
    u64 size;
    struct wramfs_node *parent;
    struct wramfs_node *children;
    struct wramfs_node *next;
};

struct wramfs {
    struct wramfs_node *root;
    u64 used_bytes;
};

static struct wramfs_node *node_new(struct wramfs_node *parent,
                                    const char *name, int is_dir)
{
    struct wramfs_node *node = kzalloc(sizeof(*node));
    if (!node)
        return NULL;
    node->is_dir = is_dir;
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->parent = parent;
    if (parent) {
        node->next = parent->children;
        parent->children = node;
    }
    return node;
}

static struct wramfs_node *child_of(struct wramfs_node *directory,
                                    const char *name)
{
    if (!directory || !directory->is_dir)
        return NULL;
    for (struct wramfs_node *child = directory->children; child;
         child = child->next) {
        if (strcmp(child->name, name) == 0)
            return child;
    }
    return NULL;
}

static struct wramfs_node *lookup(struct wramfs *fs, const char *rel)
{
    if (!fs || !fs->root)
        return NULL;
    struct wramfs_node *current = fs->root;
    const char *cursor = rel ? rel : "";
    while (*cursor) {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;
        char component[WRAMFS_NAME];
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 < sizeof(component))
                component[length] = *cursor;
            length++;
            cursor++;
        }
        if (!length || length >= sizeof(component) ||
            strcmp(component, ".") == 0 || strcmp(component, "..") == 0)
            return NULL;
        component[length] = 0;
        current = child_of(current, component);
        if (!current)
            return NULL;
    }
    return current;
}

/* Walk a relative path, creating missing directories. */
static struct wramfs_node *create(struct wramfs *fs, const char *rel,
                                  int is_dir)
{
    if (!fs || !fs->root)
        return NULL;
    struct wramfs_node *current = fs->root;
    const char *cursor = rel ? rel : "";
    while (*cursor) {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;
        char component[WRAMFS_NAME];
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 < sizeof(component))
                component[length] = *cursor;
            length++;
            cursor++;
        }
        if (!length || length >= sizeof(component) ||
            strcmp(component, ".") == 0 || strcmp(component, "..") == 0)
            return NULL;
        component[length] = 0;

        int last = 1;
        const char *peek = cursor;
        while (*peek == '/')
            peek++;
        if (*peek)
            last = 0;

        struct wramfs_node *child = child_of(current, component);
        if (!child) {
            if (!current->is_dir)
                return NULL;
            child = node_new(current, component,
                             last ? (is_dir != 0) : 1);
            if (!child)
                return NULL;
        } else if (!last && !child->is_dir) {
            return NULL;
        } else if (last && child->is_dir != (is_dir != 0)) {
            return NULL;
        }
        if (last)
            return child;
        current = child;
    }
    return current;
}

struct wramfs *wramfs_create(void)
{
    struct wramfs *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return NULL;
    fs->root = node_new(NULL, "/", 1);
    if (!fs->root) {
        kfree(fs);
        return NULL;
    }
    return fs;
}

static void free_tree(struct wramfs_node *node)
{
    while (node) {
        struct wramfs_node *next = node->next;
        free_tree(node->children);
        if (node->data)
            kfree(node->data);
        kfree(node);
        node = next;
    }
}

void wramfs_destroy(struct wramfs *fs)
{
    if (!fs)
        return;
    free_tree(fs->root);
    kfree(fs);
}

int wramfs_list(struct wramfs *fs, const char *rel, vfs_list_cb callback,
                void *context)
{
    struct wramfs_node *node = lookup(fs, rel);
    if (!node || !node->is_dir)
        return -1;
    for (struct wramfs_node *child = node->children; child;
         child = child->next)
        callback(child->name, child->size, child->is_dir, context);
    return 0;
}

int wramfs_stat(struct wramfs *fs, const char *rel, int *is_dir, u64 *size)
{
    struct wramfs_node *node = lookup(fs, rel);
    if (!node)
        return -1;
    if (is_dir)
        *is_dir = node->is_dir;
    if (size)
        *size = node->size;
    return 0;
}

int wramfs_read(struct wramfs *fs, const char *rel, u64 offset, void *buffer,
                u64 capacity, u64 *read_out)
{
    struct wramfs_node *node = lookup(fs, rel);
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

int wramfs_write(struct wramfs *fs, const char *rel, const u8 *data,
                 u64 size)
{
    if (!rel || !rel[0] || (size && !data) || size > WRAMFS_FILE_MAX)
        return -1;
    if (fs->used_bytes + size > WRAMFS_TOTAL_MAX)
        return -1;
    struct wramfs_node *node = create(fs, rel, 0);
    if (!node || node->is_dir)
        return -1;

    u8 *copy = NULL;
    if (size) {
        copy = kmalloc((usize)size);
        if (!copy)
            return -1;
        memcpy(copy, data, (usize)size);
    }
    if (node->data) {
        fs->used_bytes -= node->size;
        kfree(node->data);
    }
    node->data = copy;
    node->size = size;
    fs->used_bytes += size;
    return 0;
}

int wramfs_unlink(struct wramfs *fs, const char *rel)
{
    struct wramfs_node *node = lookup(fs, rel);
    if (!node || node == fs->root || node->children)
        return -1;
    struct wramfs_node **link = &node->parent->children;
    while (*link && *link != node)
        link = &(*link)->next;
    if (*link != node)
        return -1;
    *link = node->next;
    if (node->data) {
        fs->used_bytes -= node->size;
        kfree(node->data);
    }
    kfree(node);
    return 0;
}

static int ramfs_op_list(void *private, const char *rel,
                         vfs_list_cb callback, void *context)
{
    return wramfs_list(private, rel, callback, context);
}

static int ramfs_op_stat(void *private, const char *rel, int *is_dir,
                         u64 *size)
{
    return wramfs_stat(private, rel, is_dir, size);
}

static int ramfs_op_read(void *private, const char *rel, u64 offset,
                         void *buffer, u64 capacity, u64 *read_out)
{
    return wramfs_read(private, rel, offset, buffer, capacity, read_out);
}

static int ramfs_op_write(void *private, const char *rel, const u8 *data,
                          u64 size)
{
    return wramfs_write(private, rel, data, size);
}

static int ramfs_op_unlink(void *private, const char *rel)
{
    return wramfs_unlink(private, rel);
}

static const struct vfs_fs_ops g_ramfs_ops = {
    .name = "ramfs",
    .list = ramfs_op_list,
    .stat = ramfs_op_stat,
    .read = ramfs_op_read,
    .write = ramfs_op_write,
    .unlink = ramfs_op_unlink,
};

int ramfs_mount_at(const char *path, const char *type)
{
    struct wramfs *fs = wramfs_create();
    if (!fs)
        return -1;
    if (vfs_mount(path, type ? type : "ramfs", &g_ramfs_ops, fs) != 0) {
        wramfs_destroy(fs);
        return -1;
    }
    kprintf("[ramfs] writable ramfs mounted at %s\n", path);
    return 0;
}
