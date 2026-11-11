/*
 * Zhaoxu OS - kernel side of the Ring3 ext4 service RPC.
 *
 * The service (/bin/ext4fs) owns the ext4 implementation.  The kernel talks
 * to it through one control page (struct fs_rpc) plus one 4 KiB data page,
 * both shared through the identity map: the service maps 0..4 GiB as user
 * pages, so the physical address is a valid pointer on both sides.
 *
 * Only one request is in flight at a time; g_busy serialises callers and the
 * state machine mirrors the driver service (REQUEST -> DONE -> IDLE).  Every
 * request carries a sequence number that the service echoes back, so a late
 * answer to a timed-out request can never be read as the answer to a newer
 * one.  A timeout retires the channel for good (g_fs_broken): reusing a page
 * whose owner is still working on an old request is exactly the bug the
 * sequence number exists to prevent.
 *
 * Payload and response bytes are copied in and out of the data page inside
 * the busy window only: outside it another caller may already be using the
 * same page for its own request.
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
static volatile int g_fs_broken;
static u32 g_seq;
static char g_label[17];

/* Snapshot of the last completed request, filled under g_busy. */
struct fs_reply {
    u32 length;
    u64 value0;
    u64 value1;
};

static struct fs_reply g_reply;

u8 *fs_data(void)
{
    return g_data;
}

int fs_service_ready(void)
{
    return g_ready;
}

const char *fs_label(void)
{
    return g_label;
}

/* Copy at most capacity - 1 characters and always terminate. */
static void copy_text(char *destination, usize capacity, const char *source)
{
    usize n = 0;
    if (source) {
        while (source[n] && n + 1 < capacity) {
            destination[n] = source[n];
            n++;
        }
    }
    destination[n] = 0;
}

/* Wait until the service publishes DONE for this exact request.  A stale DONE
 * left over from an earlier request does not match the sequence number, so it
 * can neither satisfy nor poison this wait. */
static int wait_done(u32 seq)
{
    for (int waited = 0; waited < FS_WAIT_MS; waited++) {
        if (g_rpc->state == FS_STATE_DONE && g_rpc->seq == seq) {
            __sync_synchronize();
            return 0;
        }
        thread_sleep_ms(1);
    }
    return -1;
}

static void take_busy(void)
{
    while (__atomic_test_and_set(&g_busy, __ATOMIC_ACQUIRE)) {
        if (g_fs_broken)
            return; /* Not acquired: the holder is still unwinding. */
        thread_sleep_ms(1);
    }
}

static void drop_busy(void)
{
    __atomic_clear(&g_busy, __ATOMIC_RELEASE);
}

/* The one and only place that touches the control page.  `send` (length
 * bytes, at most FS_DATA_SIZE) is copied into the data page after g_busy is
 * taken and `recv` (at most `length` bytes) is copied out before it is
 * released.  `label` is the volume label for FORMAT. */
static int fs_call_values(u32 op, const char *path, u64 offset, u32 length,
                          u32 flags, u64 value0, u64 value1,
                          const char *label, const void *send, void *recv)
{
    g_reply.length = 0;
    g_reply.value0 = 0;
    g_reply.value1 = 0;

    if (!g_rpc || !g_ready || g_fs_broken)
        return -1;

    take_busy();
    if (g_fs_broken)
        return -1; /* take_busy() returned without the lock. */
    if (!g_rpc || !g_ready) {
        drop_busy();
        return -1;
    }

    g_rpc->op = op;
    g_rpc->status = -1;
    g_rpc->flags = flags;
    g_rpc->length = length;
    g_rpc->offset = offset;
    g_rpc->value0 = value0;
    g_rpc->value1 = value1;
    copy_text(g_rpc->path, sizeof(g_rpc->path), path);
    copy_text(g_rpc->label, sizeof(g_rpc->label), label);

    u32 capacity = length < FS_DATA_SIZE ? length : FS_DATA_SIZE;
    if (send && capacity)
        memcpy(g_data, send, capacity);

    u32 seq = ++g_seq;
    g_rpc->seq = seq;
    __sync_synchronize();
    g_rpc->state = FS_STATE_REQUEST;

    if (wait_done(seq) != 0) {
        /* The service did not answer in time.  It may still be inside the
         * old request, so the page must never be reused: retire the channel
         * and fail every later call instead of corrupting a new request. */
        g_fs_broken = 1;
        g_ready = 0;
        drop_busy();
        return -1;
    }

    int status = g_rpc->status;
    if (status == 0) {
        u32 received = g_rpc->length;
        if (received > capacity)
            received = capacity;
        g_reply.length = received;
        g_reply.value0 = g_rpc->value0;
        g_reply.value1 = g_rpc->value1;
        if (recv && received)
            memcpy(recv, g_data, received);
        if (op == FS_OP_MOUNT || op == FS_OP_FORMAT || op == FS_OP_SYNC) {
            /* The service republishes the label for every operation that can
             * change which volume is in use, so the cache is never stale. */
            memcpy(g_label, g_rpc->label, sizeof(g_label));
            g_label[sizeof(g_label) - 1] = 0;
        }
    }

    g_rpc->state = FS_STATE_IDLE;
    drop_busy();
    return status;
}

static int fs_call(u32 op, const char *path, u64 offset, u32 length,
                   u32 flags)
{
    return fs_call_values(op, path, offset, length, flags, 0, 0, 0, 0, 0);
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
        *is_dir = (int)g_reply.value0;
    if (size)
        *size = g_reply.value1;
    return 0;
}

int fs_call_list(const char *path, u32 cursor, void *out, u32 cap,
                 u32 *written, u32 *next_cursor, u32 *count)
{
    if (!out || !cap)
        return -1;
    if (fs_call_values(FS_OP_LIST, path, cursor, cap, 0, 0, 0, 0, 0,
                       out) != 0)
        return -1;
    if (written)
        *written = g_reply.length;
    if (next_cursor)
        *next_cursor = (u32)g_reply.value0;
    if (count)
        *count = (u32)g_reply.value1;
    return 0;
}

int fs_call_read(const char *path, u64 offset, void *dst, u32 length,
                 u32 *got)
{
    if (!dst && length)
        return -1;
    if (length > FS_DATA_SIZE)
        length = FS_DATA_SIZE;
    if (fs_call_values(FS_OP_READ, path, offset, length, 0, 0, 0, 0, 0,
                       dst) != 0)
        return -1;
    if (got)
        *got = g_reply.length;
    return 0;
}

int fs_call_write(const char *path, u64 total, u64 offset,
                  const void *src, u32 length, int truncate_first)
{
    if (length > FS_DATA_SIZE || (length && !src))
        return -1;
    return fs_call_values(FS_OP_WRITE, path, offset, length,
                          truncate_first ? FS_FLAG_TRUNCATE : 0, total, 0, 0,
                          src, 0);
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
    if (!g_rpc || !g_ready || g_fs_broken)
        return -1;
    u64 encoded = ((u64)(u32)disk << 48) | (base & 0xFFFFFFFFFFFFULL);
    return fs_call_values(FS_OP_FORMAT, 0, 0, 0, 0, encoded, sectors, label,
                          0, 0);
}

int fs_call_write_contig(const char *path, u64 total, u64 offset,
                         const void *src, u32 length, u32 *first,
                         u32 *blocks)
{
    if (length > FS_DATA_SIZE || (length && !src))
        return -1;
    if (fs_call_values(FS_OP_WRITE_CONTIG, path, offset, length, 0, total, 0,
                       0, src, 0) != 0)
        return -1;
    if (offset + length >= total) {
        if (first)
            *first = (u32)g_reply.value0;
        if (blocks)
            *blocks = (u32)g_reply.value1;
    }
    return 0;
}

int fs_service_start(void)
{
    u64 control = pmm_alloc_page();
    u64 data = pmm_alloc_page();
    if (!control || !data)
        return -1;
    g_rpc = (struct fs_rpc *)(uintptr_t)control;
    g_data = (u8 *)(uintptr_t)data;
    g_ready = 0;
    g_mounted = 0;
    g_fs_broken = 0;
    g_seq = 0;
    g_label[0] = 0;
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
