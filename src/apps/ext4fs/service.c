/*
 * Zhaoxu OS - ext4fs service request loop.
 *
 * One request at a time: the kernel fills struct fs_rpc, flips the state to
 * REQUEST and waits for DONE.  WRITE requests are streamed by the kernel in
 * FS_DATA_SIZE chunks into an 8 MiB shared memory buffer and committed to the
 * volume when the last chunk arrives.
 */
#include <ext4fs.h>
#include <fs_rpc.h>
#include <string.h>
#include <types.h>
#include <userlib.h>

#include "ext4.h"

#define FS_WRITE_MAX (8u * 1024u * 1024u)

static u8 *g_write_buf;
static struct ext4_state g_saved;
static int g_install_active;
/* Next offset the kernel must send for a streamed write.  A dropped or
 * reordered chunk breaks the chain instead of overwriting good bytes with
 * stale ones. */
static u64 g_write_expected;

/* Reject a chunk the write buffer cannot hold.  The comparisons are ordered
 * so the sum can never wrap. */
static int write_range_ok(const struct fs_rpc *rpc)
{
    if (rpc->length > FS_DATA_SIZE || rpc->offset > FS_WRITE_MAX)
        return -1;
    if (rpc->offset > FS_WRITE_MAX - (u64)rpc->length)
        return -1;
    return 0;
}

struct list_item {
    u8 name_len;
    u8 is_dir;
    u16 pad;
    u64 size;
    char name[];
};

struct list_ctx {
    u8 *data;
    u32 cap;
    u32 used;
    u32 index;
    u32 skip;
    u32 next;
    u32 count;
};

static void list_cb(const char *name, u64 size, int is_dir, void *context)
{
    struct list_ctx *ctx = context;
    u32 index = ctx->index++;
    if (index < ctx->skip)
        return;

    u32 name_len = (u32)u_strlen(name);
    u32 need = (16u + name_len + 1u + 15u) & ~15u;
    if (ctx->used + need > ctx->cap)
        return;

    struct list_item *item = (struct list_item *)(ctx->data + ctx->used);
    item->name_len = (u8)name_len;
    item->is_dir = (u8)(is_dir != 0);
    item->pad = 0;
    item->size = size;
    memcpy(item->name, name, name_len + 1);
    ctx->used += need;
    ctx->count++;
    ctx->next = index + 1;
}

/* The write buffer is mapped on the first write: a disk-less boot never pays
 * for 8 MiB of shared memory it will not use. */
static int ensure_write_buf(void)
{
    if (g_write_buf)
        return 0;
    long id = sys_shm_create(FS_WRITE_MAX);
    if (id <= 0)
        return -1;
    g_write_buf = (u8 *)sys_shm_map(id);
    return g_write_buf ? 0 : -1;
}

static void publish_label(struct fs_rpc *rpc)
{
    const char *label = ext4_label();
    usize n = 0;
    while (label && label[n] && n + 1 < sizeof(rpc->label)) {
        rpc->label[n] = label[n];
        n++;
    }
    rpc->label[n] = 0;
}

static int handle_mount(struct fs_rpc *rpc)
{
    if (!ext4_mounted() && ext4_mount() != 0)
        return -1;
    publish_label(rpc);
    return 0;
}

static int handle(struct fs_rpc *rpc, u8 *data)
{
    switch (rpc->op) {
    case FS_OP_MOUNT:
        return handle_mount(rpc);

    case FS_OP_STAT: {
        int is_dir = 0;
        u64 size = 0;
        if (ext4_stat(rpc->path, &is_dir, &size) != 0)
            return -1;
        rpc->value0 = is_dir ? 1 : 0;
        rpc->value1 = size;
        return 0;
    }

    case FS_OP_LIST: {
        struct list_ctx ctx = {data, FS_DATA_SIZE, 0, 0,
                               (u32)rpc->offset, 0, 0};
        if (ext4_list(rpc->path, list_cb, &ctx) != 0)
            return -1;
        rpc->length = ctx.used;
        rpc->value0 = ctx.next;
        rpc->value1 = ctx.count;
        return 0;
    }

    case FS_OP_READ: {
        u64 got = 0;
        u32 length = rpc->length;
        rpc->length = 0;
        if (length > FS_DATA_SIZE)
            length = FS_DATA_SIZE;
        if (!length)
            return 0;
        if (ext4_read_at(rpc->path, rpc->offset, data, length, &got) != 0)
            return -1;
        if (got > FS_DATA_SIZE)
            got = FS_DATA_SIZE;
        rpc->length = (u32)got;
        return 0;
    }

    case FS_OP_WRITE: {
        /* The kernel streams one file in ascending order; a chunk that
         * arrives out of order means data was lost, so fail instead of
         * committing a mixed buffer. */
        if (!ext4_mounted() || write_range_ok(rpc) != 0)
            return -1;
        if (rpc->flags & FS_FLAG_TRUNCATE)
            g_write_expected = 0;
        if (rpc->offset != g_write_expected) {
            g_write_expected = 0;
            return -1;
        }
        if (ensure_write_buf() != 0)
            return -1;
        memcpy(g_write_buf + rpc->offset, data, rpc->length);
        u64 end = rpc->offset + rpc->length;
        g_write_expected = end;
        if (end < rpc->value0)
            return 0;
        g_write_expected = 0;
        return ext4_write_file(rpc->path, g_write_buf, rpc->value0);
    }

    case FS_OP_WRITE_CONTIG: {
        if (!ext4_mounted() || write_range_ok(rpc) != 0)
            return -1;
        if (rpc->flags & FS_FLAG_TRUNCATE)
            g_write_expected = 0;
        if (rpc->offset != g_write_expected) {
            g_write_expected = 0;
            return -1;
        }
        if (ensure_write_buf() != 0)
            return -1;
        memcpy(g_write_buf + rpc->offset, data, rpc->length);
        u64 end = rpc->offset + rpc->length;
        g_write_expected = end;
        if (end < rpc->value0)
            return 0;
        g_write_expected = 0;
        u32 first = 0;
        u32 blocks = 0;
        if (ext4_write_file_contig(rpc->path, g_write_buf, rpc->value0,
                                   &first, &blocks) != 0)
            return -1;
        rpc->value0 = first;
        rpc->value1 = blocks;
        return 0;
    }

    case FS_OP_UNLINK:
        return ext4_unlink(rpc->path);

    case FS_OP_SYNC:
        if (ext4_sync() != 0)
            return -1;
        if (g_install_active) {
            ext4_state_restore(g_saved);
            g_install_active = 0;
        }
        /* The volume in use may just have changed back: republish the label
         * so the kernel's cache never describes a volume that is gone. */
        publish_label(rpc);
        return 0;

    case FS_OP_FORMAT: {
        u64 encoded = rpc->value0;
        int disk = (int)(encoded >> 48);
        u64 base = encoded & 0xFFFFFFFFFFFFULL;
        g_saved = ext4_state_save();
        g_install_active = 1;
        if (ext4_format_volume(disk, base, (u32)rpc->value1,
                               rpc->label) != 0 ||
            ext4_mount_at(disk, base) != 0) {
            ext4_state_restore(g_saved);
            g_install_active = 0;
            return -1;
        }
        return 0;
    }

    default:
        return -1;
    }
}

/* Readiness is announced only after the block environment is armed and the
 * startup probe has finished, so the kernel cannot issue a request while the
 * probe still owns the volume. */
void ext4fs_service(struct fs_rpc *rpc, u8 *data)
{
    ext4fs_block_env_install();
    ext4fs_probe();

    __sync_synchronize();
    rpc->state = FS_STATE_READY;

    for (;;) {
        if (rpc->state == FS_STATE_REQUEST) {
            /* Echo the request generation: status first, then the release
             * barrier, then seq and state, so the kernel never observes a
             * DONE that belongs to a different request. */
            u32 seq = rpc->seq;
            __sync_synchronize();
            i32 status = handle(rpc, data);
            rpc->status = status;
            __sync_synchronize();
            rpc->seq = seq;
            __sync_synchronize();
            rpc->state = FS_STATE_DONE;
        } else {
            sys_sleep(1);
        }
    }
}
