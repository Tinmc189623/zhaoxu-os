#ifndef ZHAOXU_CORE_ABI_H
#define ZHAOXU_CORE_ABI_H

/*
 * Shared ABI between the Yuanshuo core and the FreeBSD kernel build (M2).
 *
 * This header is compiled on both sides: the core includes it with its own
 * types.h, and the FreeBSD build includes it with _KERNEL defined.  Nothing
 * from either kernel's private headers leaks across the boundary; the table
 * below is the whole contract.
 */

#if defined(_KERNEL) && !defined(ZHAOXU_KERNEL)
#include <sys/types.h>
typedef uint8_t zx_u8;
typedef uint32_t zx_u32;
typedef uint64_t zx_u64;
#else
#include <types.h>
typedef u8 zx_u8;
typedef u32 zx_u32;
typedef u64 zx_u64;
#endif

#define ZX_CORE_SERVICES_VERSION 4
#define ZX_IDENTITY_LIMIT (64ULL * 1024ULL * 1024ULL * 1024ULL)

typedef int (*zx_trap_fn)(void *frame, void *ctx);
typedef void (*zx_timer_fn)(void *ctx);
typedef void (*zx_task_fn)(void *arg);

typedef void (*zx_list_fn)(const char *name, zx_u64 size, int is_dir,
                           void *context);

/* Rule 51: VFS belongs to the BSD layer.  The personality registers this
 * provider once its vnode layer is up; until then the core provider serves
 * the live root. */
struct zx_vfs_provider {
    const char *name;
    int (*list)(const char *path, zx_list_fn callback, void *context);
    int (*stat)(const char *path, int *is_dir, zx_u64 *size);
    int (*read_at)(const char *path, zx_u64 offset, zx_u8 *dst, zx_u64 cap,
                   zx_u64 *read_out);
    int (*write_file)(const char *path, const zx_u8 *data, zx_u64 size);
    int (*unlink)(const char *path);
    int (*mkdir)(const char *path);
    int (*rmdir)(const char *path);
    int (*rename)(const char *from, const char *to);
};

struct zx_core_services {
    zx_u32 version;
    zx_u32 page_size;
    zx_u64 kernel_va_base;   /* first kernel virtual address */
    zx_u64 kernel_va_end;    /* end of the kernel image window */
    zx_u64 identity_limit;   /* physical memory reachable at its own address */

    /* Output for the personality's cn(4) backend. */
    void (*console_write)(const char *text, zx_u32 length);

    /* The VM window the core grants the personality. */
    zx_u64 (*vm_base)(void);
    zx_u32 (*vm_pages)(void);
    int (*vm_map)(zx_u32 index, zx_u64 flags);
    int (*vm_unmap)(zx_u32 index);
    int (*vm_phys)(zx_u32 index, zx_u64 *physical);

    /* Scheduling, traps, timers, IPC: the M1 personality API. */
    void *(*task_create)(const char *name, zx_task_fn entry, void *arg);
    int (*trap_register)(zx_u8 vector, zx_trap_fn fn, void *ctx);
    int (*timer_register)(zx_u32 period_ticks, zx_timer_fn fn, void *ctx);
    zx_u64 (*ipc_create)(zx_u64 *peer);
    int (*ipc_send)(zx_u64 endpoint, zx_u64 type, const void *data,
                    zx_u32 length);
    int (*ipc_recv)(zx_u64 endpoint, zx_u64 *type, void *data,
                    zx_u32 capacity, zx_u32 timeout_ms);

    /* BSD VFS takeover (rule 51).  Passing NULL hands the path operations
     * back to the core provider. */
    int (*vfs_register)(const struct zx_vfs_provider *provider);
};

#endif
