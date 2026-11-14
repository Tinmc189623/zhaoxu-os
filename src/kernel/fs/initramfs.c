/*
 * Zhaoxu OS - initramfs
 *
 * The boot image carries a ustar archive built from src/initramfs/.  The
 * archive is linked into the kernel as a read-only blob; this file parses it
 * once and exposes the entries as a small in-memory file tree.
 */
#include <inflate.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

extern const u8 initramfs_start[];
extern const u8 initramfs_end[];

/* The blob embedded in kernel.elf is a ZIR1-wrapped zlib stream when built
 * through the Makefile, or a plain ustar archive for hand-built kernels. */
static const u8 *g_archive;
static u32 g_archive_size;

#define RAMFS_NAME_MAX 64
#define RAMFS_PATH_MAX 256

enum ram_node_type {
    RAM_DIR = 1,
    RAM_FILE = 2
};

struct ram_node {
    char name[RAMFS_NAME_MAX];
    u8 type;
    u64 size;
    const u8 *data;
    struct ram_node *children;
    struct ram_node *next;
};

static struct ram_node *g_root;
static u64 g_file_count;
static u64 g_total_bytes;

static struct ram_node *ram_child(struct ram_node *directory, const char *name)
{
    if (!directory || directory->type != RAM_DIR)
        return NULL;
    for (struct ram_node *child = directory->children; child;
         child = child->next) {
        if (strcmp(child->name, name) == 0)
            return child;
    }
    return NULL;
}

static struct ram_node *ram_create(struct ram_node *directory, const char *name,
                                   u8 type)
{
    struct ram_node *node = kzalloc(sizeof(*node));
    if (!node)
        return NULL;
    strncpy(node->name, name, RAMFS_NAME_MAX - 1);
    node->type = type;
    node->next = directory->children;
    directory->children = node;
    return node;
}

static struct ram_node *ram_lookup(const char *path)
{
    if (!g_root)
        return NULL;
    if (!path || !*path || strcmp(path, "/") == 0)
        return g_root;

    struct ram_node *current = g_root;
    const char *cursor = path;

    while (*cursor) {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;

        char component[RAMFS_NAME_MAX];
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 < sizeof(component))
                component[length] = *cursor;
            length++;
            cursor++;
        }
        if (length >= sizeof(component))
            return NULL;
        component[length] = 0;

        if (current->type != RAM_DIR)
            return NULL;
        current = ram_child(current, component);
        if (!current)
            return NULL;
    }
    return current;
}

/* Create missing parent directories and return the final node. */
static struct ram_node *ram_add(const char *path, u8 type, const u8 *data,
                                u64 size)
{
    if (!g_root)
        return NULL;

    struct ram_node *current = g_root;
    const char *cursor = path;

    while (*cursor) {
        while (*cursor == '/')
            cursor++;
        if (!*cursor)
            break;

        char component[RAMFS_NAME_MAX];
        usize length = 0;
        while (*cursor && *cursor != '/') {
            if (length + 1 < sizeof(component))
                component[length] = *cursor;
            length++;
            cursor++;
        }
        if (length == 0 || length >= sizeof(component))
            return NULL;
        component[length] = 0;

        int last = 1;
        const char *peek = cursor;
        while (*peek == '/')
            peek++;
        if (*peek)
            last = 0;

        struct ram_node *child = ram_child(current, component);
        if (!child) {
            child = ram_create(current, component,
                               last ? type : RAM_DIR);
            if (!child)
                return NULL;
        } else if (!last && child->type != RAM_DIR) {
            return NULL;
        }

        if (last) {
            if (type == RAM_FILE) {
                child->type = RAM_FILE;
                child->data = data;
                child->size = size;
                g_file_count++;
                g_total_bytes += size;
            }
            return child;
        }
        current = child;
    }
    return current;
}

static u64 parse_octal(const char *text, u32 length)
{
    u64 value = 0;
    u32 index = 0;
    while (index < length && (text[index] == ' ' || text[index] == 0))
        index++;
    while (index < length && text[index] >= '0' && text[index] <= '7') {
        value = (value << 3) + (u64)(text[index] - '0');
        index++;
    }
    return value;
}

static void copy_field(char *destination, usize capacity, const char *source,
                       usize length)
{
    usize index = 0;
    while (index + 1 < capacity && index < length && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = 0;
}

/* Unpack the embedded archive once.  A 64 MiB sanity cap keeps a corrupt
 * header from asking the page allocator for an absurd region. */
static int archive_open(void)
{
    u32 size = (u32)(initramfs_end - initramfs_start);
    if (size >= 8 && memcmp(initramfs_start, "ZIR1", 4) == 0) {
        u32 raw = 0;
        memcpy(&raw, initramfs_start + 4, 4);
        if (raw == 0 || raw > 64u * 1024u * 1024u)
            return -1;
        u32 pages = (raw + PAGE_SIZE - 1) / PAGE_SIZE;
        u64 region = vmm_alloc_pages(pages);
        if (!region)
            return -1;
        u32 out = 0;
        if (zlib_decompress(initramfs_start + 8, size - 8,
                            (u8 *)(uintptr_t)region, raw, &out) != 0 ||
            out != raw) {
            kprintf("[vfs ] initramfs zlib inflate failed (%u bytes)\n", raw);
            return -1;
        }
        g_archive = (const u8 *)(uintptr_t)region;
        g_archive_size = raw;
        kprintf("[vfs ] initramfs zlib %u -> %u bytes\n", size, raw);
        return 0;
    }
    g_archive = initramfs_start;
    g_archive_size = size;
    return 0;
}

static int parse_archive(void)
{
    const u8 *cursor = g_archive;
    const u8 *end = g_archive + g_archive_size;

    while (cursor + 512 <= end) {
        const char *header = (const char *)cursor;

        int empty = 1;
        for (u32 i = 0; i < 512; i++) {
            if (header[i]) {
                empty = 0;
                break;
            }
        }
        if (empty)
            break;

        char name[100];
        char prefix[156];
        copy_field(name, sizeof(name), header, 100);
        copy_field(prefix, sizeof(prefix), header + 345, 155);

        u64 size = parse_octal(header + 124, 12);
        char type = header[156];

        char path[RAMFS_PATH_MAX];
        usize used = 0;
        if (prefix[0]) {
            while (prefix[used] && used + 1 < sizeof(path)) {
                path[used] = prefix[used];
                used++;
            }
            if (used + 1 < sizeof(path))
                path[used++] = '/';
        }
        for (usize i = 0; name[i] && used + 1 < sizeof(path); i++)
            path[used++] = name[i];
        path[used] = 0;

        /* Normalise leading "./" and "/" and trailing '/'. */
        char *start = path;
        while (start[0] == '.' && start[1] == '/')
            start += 2;
        while (*start == '/')
            start++;
        usize length = strlen(start);
        while (length > 0 && start[length - 1] == '/')
            start[--length] = 0;

        const u8 *data = cursor + 512;
        if (cursor + 512 + size > end)
            break;

        if (length > 0) {
            if (type == '5')
                ram_add(start, RAM_DIR, NULL, 0);
            else if (type == '0' || type == 0)
                ram_add(start, RAM_FILE, data, size);
        }

        u64 advance = 512 + ((size + 511) & ~511ULL);
        cursor += advance;
    }
    return 0;
}

int ramfs_init(void)
{
    g_root = kzalloc(sizeof(*g_root));
    if (!g_root)
        return -1;
    strcpy(g_root->name, "/");
    g_root->type = RAM_DIR;
    g_file_count = 0;
    g_total_bytes = 0;

    if (archive_open() != 0) {
        kprintf("[vfs ] initramfs archive unavailable\n");
        return -1;
    }
    parse_archive();

    kprintf("[vfs ] initramfs: %llu files, %llu bytes\n",
            g_file_count, g_total_bytes);
    return 0;
}

int ramfs_list(const char *path, vfs_list_cb callback, void *context)
{
    struct ram_node *node = ram_lookup(path);
    if (!node || node->type != RAM_DIR)
        return -1;
    for (struct ram_node *child = node->children; child; child = child->next)
        callback(child->name, child->size, child->type == RAM_DIR, context);
    return 0;
}

int ramfs_read_all(const char *path, u8 **buffer, u64 *size)
{
    struct ram_node *node = ram_lookup(path);
    if (!node || node->type != RAM_FILE)
        return -1;

    u8 *copy = kmalloc(node->size ? node->size : 1);
    if (!copy)
        return -1;
    if (node->size)
        memcpy(copy, node->data, node->size);
    *buffer = copy;
    if (size)
        *size = node->size;
    return 0;
}

int ramfs_read_into(const char *path, u8 *dst, u64 cap, u64 *read_out)
{
    struct ram_node *node = ram_lookup(path);
    if (!node || node->type != RAM_FILE || !dst || node->size > cap)
        return -1;
    if (node->size)
        memcpy(dst, node->data, node->size);
    if (read_out)
        *read_out = node->size;
    return 0;
}

int ramfs_stat(const char *path, int *is_dir, u64 *size)
{
    struct ram_node *node = ram_lookup(path);
    if (!node)
        return -1;
    if (is_dir)
        *is_dir = node->type == RAM_DIR;
    if (size)
        *size = node->size;
    return 0;
}

/* Offset read used by the VFS file-descriptor path.  The archive itself is
 * immutable, so this is a straight copy out of the linked ustar blob. */
int ramfs_read_at(const char *path, u64 offset, u8 *dst, u64 cap,
                  u64 *read_out)
{
    struct ram_node *node = ram_lookup(path);
    if (!node || node->type != RAM_FILE || (!dst && cap))
        return -1;
    if (offset >= node->size) {
        if (read_out)
            *read_out = 0;
        return 0;
    }
    u64 available = node->size - offset;
    u64 count = cap < available ? cap : available;
    if (count)
        memcpy(dst, node->data + offset, (usize)count);
    if (read_out)
        *read_out = count;
    return 0;
}

/* Mount-relative path to the absolute path used by the ramfs lookup. */
static int ramfs_op_path(const char *rel, char *path, usize capacity)
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

static int ramfs_list_op(void *private, const char *rel,
                         vfs_list_cb callback, void *context)
{
    char path[VFS_PATH_MAX];
    (void)private;
    if (ramfs_op_path(rel, path, sizeof(path)) != 0)
        return -1;
    return ramfs_list(path, callback, context);
}

static int ramfs_stat_op(void *private, const char *rel, int *is_dir,
                         u64 *size)
{
    char path[VFS_PATH_MAX];
    (void)private;
    if (ramfs_op_path(rel, path, sizeof(path)) != 0)
        return -1;
    return ramfs_stat(path, is_dir, size);
}

static int ramfs_read_op(void *private, const char *rel, u64 offset,
                         void *buffer, u64 capacity, u64 *read_out)
{
    char path[VFS_PATH_MAX];
    (void)private;
    if (ramfs_op_path(rel, path, sizeof(path)) != 0)
        return -1;
    return ramfs_read_at(path, offset, (u8 *)buffer, capacity, read_out);
}

static int ramfs_write_op(void *private, const char *rel, const u8 *data,
                          u64 size)
{
    (void)private;
    (void)rel;
    (void)data;
    (void)size;
    return -1;
}

static int ramfs_unlink_op(void *private, const char *rel)
{
    (void)private;
    (void)rel;
    return -1;
}

static const struct vfs_fs_ops g_ramfs_ops = {
    .name = "initramfs",
    .list = ramfs_list_op,
    .stat = ramfs_stat_op,
    .read = ramfs_read_op,
    .write = ramfs_write_op,
    .unlink = ramfs_unlink_op,
};

int ramfs_mount_root(void)
{
    return vfs_mount("/", "rootfs", &g_ramfs_ops, NULL);
}
