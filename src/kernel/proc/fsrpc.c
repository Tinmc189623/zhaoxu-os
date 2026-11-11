/*
 * Zhaoxu OS - kernel side of the Ring3 ext4 service RPC.
 *
 * The service (/bin/ext4fs) owns the ext4 implementation.  The kernel talks
 * to it through one control page (struct fs_rpc) plus one 4 KiB data page,
 * both shared through the identity map: the service maps 0..4 GiB as user
 * pages, so the physical address is a valid pointer on both sides.
 *
 * Only one request is in flight at a time; g_busy serialises callers and the
 * state machine mirrors the driver service (REQUEST -> DONE -> IDLE).
 */
#include <fs_rpc.h>
#include <mem.h>
#include <process.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <types.h>

#define FS_WAIT_MS 5000

static struct fs_rpc *g_rpc;
static u8 *g_data;
static int g_ready;
static int g_mounted;
static volatile int g_busy;

u8 *fs_data(void)
{
    return g_data;
}

int fs_service_ready(void)
{
    return g_ready;
}

static int wait_done(void)
{
    for (int waited = 0; waited < FS_WAIT_MS; waited++) {
        if (g_rpc->state == FS_STATE_DONE)
            return 0;
        thread_sleep_ms(1);
    }
    g_rpc->state = FS_STATE_IDLE;
    return -1;
}

static int fs_call_values(u32 op, const char *path, u64 offset, u32 length,
                          u32 flags, u64 value0, u64 value1)
{
    if (!g_rpc || !g_ready)
        return -1;

    while (__atomic_test_and_set(&g_busy, __ATOMIC_ACQUIRE))
        thread_sleep_ms(1);

    g_rpc->op = op;
    g_rpc->status = -1;
    g_rpc->flags = flags;
    g_rpc->length = length;
    g_rpc->offset = offset;
    g_rpc->value0 = value0;
    g_rpc->value1 = value1;
    g_rpc->path[0] = 0;
    if (path) {
        usize n = 0;
        while (path[n] && n + 1 < sizeof(g_rpc->path)) {
            g_rpc->path[n] = path[n];
            n++;
        }
        g_rpc->path[n] = 0;
    }
    __sync_synchronize();
    g_rpc->state = FS_STATE_REQUEST;
    int status = wait_done() == 0 ? g_rpc->status : -1;
    g_rpc->state = FS_STATE_IDLE;
    __atomic_clear(&g_busy, __ATOMIC_RELEASE);
    return status;
}

static int fs_call(u32 op, const char *path, u64 offset, u32 length,
                   u32 flags)
{
    return fs_call_values(op, path, offset, length, flags, 0, 0);
}

/* Idempotent: a second mount after the service already mounted a volume is a
 * success, not a re-probe. */
int fs_call_mount(void)
{
    if (g_mounted)
        return 0;
    if (fs_call(FS_OP_MOUNT, 0, 0, 0, 0) != 0)
        return -1;
    g_mounted = 1;
    return 0;
}

int fs_call_stat(const char *path, int *is_dir, u64 *size)
{
    if (fs_call(FS_OP_STAT, path, 0, 0, 0) != 0)
        return -1;
    if (is_dir)
        *is_dir = (int)g_rpc->value0;
    if (size)
        *size = g_rpc->value1;
    return 0;
}

int fs_call_list(const char *path, u32 cursor, void *out, u32 cap,
                 u32 *written, u32 *next_cursor, u32 *count)
{
    if (fs_call(FS_OP_LIST, path, cursor, cap, 0) != 0)
        return -1;
    u32 length = g_rpc->length;
    if (length > FS_DATA_SIZE)
        return -1;
    if (length > cap)
        length = cap;
    if (written)
        *written = length;
    if (next_cursor)
        *next_cursor = (u32)g_rpc->value0;
    if (count)
        *count = (u32)g_rpc->value1;
    if (out && length)
        memcpy(out, g_data, length);
    return 0;
}

int fs_call_read(const char *path, u64 offset, void *dst, u32 length,
                 u32 *got)
{
    if (length > FS_DATA_SIZE)
        length = FS_DATA_SIZE;
    if (fs_call(FS_OP_READ, path, offset, length, 0) != 0)
        return -1;
    u32 read = g_rpc->length;
    if (read > FS_DATA_SIZE)
        return -1;
    if (got)
        *got = read;
    if (dst && read)
        memcpy(dst, g_data, read);
    return 0;
}

int fs_call_write(const char *path, u64 total, u64 offset,
                  const void *src, u32 length, int truncate_first)
{
    if (!g_data || length > FS_DATA_SIZE)
        return -1;
    if (length && src)
        memcpy(g_data, src, length);
    return fs_call_values(FS_OP_WRITE, path, offset, length,
                          truncate_first ? FS_FLAG_TRUNCATE : 0, total, 0);
}

int fs_call_unlink(const char *path)
{
    return fs_call(FS_OP_UNLINK, path, 0, 0, 0);
}

int fs_call_sync(void)
{
    return fs_call(FS_OP_SYNC, 0, 0, 0, 0);
}

int fs_call_format(int disk, u64 base, u32 sectors, const char *label)
{
    usize n = 0;
    while (label && label[n] && n + 1 < sizeof(g_rpc->label)) {
        g_rpc->label[n] = label[n];
        n++;
    }
    if (label)
        g_rpc->label[n] = 0;
    u64 encoded = ((u64)(u32)disk << 48) | (base & 0xFFFFFFFFFFFFULL);
    return fs_call_values(FS_OP_FORMAT, 0, 0, 0, 0, encoded, sectors);
}

int fs_call_write_contig(const char *path, u64 total, u64 offset,
                         const void *src, u32 length, u32 *first,
                         u32 *blocks)
{
    if (!g_data || length > FS_DATA_SIZE)
        return -1;
    if (length && src)
        memcpy(g_data, src, length);
    if (fs_call_values(FS_OP_WRITE_CONTIG, path, offset, length, 0, total,
                       0) != 0)
        return -1;
    if (offset + length >= total) {
        if (first)
            *first = (u32)g_rpc->value0;
        if (blocks)
            *blocks = (u32)g_rpc->value1;
    }
    return 0;
}

const char *fs_label(void)
{
    return g_rpc ? g_rpc->label : "";
}

int fs_service_start(void)
{
    u64 control = pmm_alloc_page();
    u64 data = pmm_alloc_page();
    if (!control || !data)
        return -1;
    g_rpc = (struct fs_rpc *)(uintptr_t)control;
    g_data = (u8 *)(uintptr_t)data;
    memset(g_rpc, 0, PAGE_SIZE);
    memset(g_data, 0, PAGE_SIZE);
    g_rpc->magic = FS_RPC_MAGIC;
    g_rpc->state = FS_STATE_IDLE;
    g_rpc->data_phys = data;

    static char argument[32];
    usize len = 0;
    argument[len++] = '0';
    argument[len++] = 'x';
    static const char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        argument[len++] = digits[(control >> shift) & 0xF];
    argument[len] = 0;
    char *argv[1] = {argument};
    if (!process_spawn("/bin/ext4fs", 1, argv))
        return -1;

    for (int waited = 0; waited < FS_WAIT_MS; waited++) {
        if (g_rpc->state == FS_STATE_READY) {
            g_ready = 1;
            return 0;
        }
        thread_sleep_ms(1);
    }
    return -1;
}
