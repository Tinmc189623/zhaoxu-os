#ifndef ZHAOXU_FS_RPC_H
#define ZHAOXU_FS_RPC_H

#include <types.h>

#define FS_RPC_MAGIC 0x3146535Au /* "ZSF1" */
#define FS_DATA_SIZE 4096u

enum fs_state {
    FS_STATE_IDLE = 0,
    FS_STATE_REQUEST = 1,
    FS_STATE_DONE = 2,
    FS_STATE_READY = 3
};

enum fs_op {
    FS_OP_NONE = 0,
    FS_OP_MOUNT = 1,
    FS_OP_STAT = 2,
    FS_OP_LIST = 3,
    FS_OP_READ = 4,
    FS_OP_WRITE = 5,
    FS_OP_UNLINK = 6,
    FS_OP_SYNC = 7,
    FS_OP_FORMAT = 8,
    FS_OP_WRITE_CONTIG = 9
};

#define FS_FLAG_TRUNCATE 0x0001u

struct fs_rpc {
    volatile u32 magic;
    volatile u32 state;
    u32 op;
    i32 status;
    u32 flags;
    u32 length;
    u64 offset;
    u64 value0;
    u64 value1;
    u64 data_phys;
    char path[256];
    char label[17];
};

/* Kernel-side client (kernel/proc/fsrpc.c).  Every call returns 0 on success
 * and -1 on failure.  The service itself never calls these. */
int fs_service_start(void);
int fs_service_ready(void);
u8 *fs_data(void);

int fs_call_mount(void);
int fs_call_stat(const char *path, int *is_dir, u64 *size);
int fs_call_list(const char *path, u32 cursor, void *out, u32 cap,
                 u32 *written, u32 *next_cursor, u32 *count);
int fs_call_read(const char *path, u64 offset, void *dst, u32 length,
                 u32 *got);
int fs_call_write(const char *path, u64 total, u64 offset, const void *src,
                  u32 length, int truncate_first);
int fs_call_unlink(const char *path);
int fs_call_sync(void);
int fs_call_format(int disk, u64 base, u32 sectors, const char *label);
int fs_call_write_contig(const char *path, u64 total, u64 offset,
                         const void *src, u32 length, u32 *first,
                         u32 *blocks);
const char *fs_label(void);

#endif
