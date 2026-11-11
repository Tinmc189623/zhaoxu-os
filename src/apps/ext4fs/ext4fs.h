#ifndef ZHAOXU_EXT4FS_SERVICE_H
#define ZHAOXU_EXT4FS_SERVICE_H

#include <fs_rpc.h>
#include <types.h>

/* service.c: the request loop, driven by the kernel through struct fs_rpc. */
void ext4fs_service(struct fs_rpc *rpc, u8 *data);

/* blk.c: wires the ext4 core to the trusted block syscalls. */
void ext4fs_block_env_install(void);

/* blk.c: Ring3 console sink used by kprintf in the linked kernel/lib/printf.c
 * (same prototypes as kernel/include/console.h). */
void console_write(const char *text);
void console_write_n(const char *text, usize length);

/* main.c: one MOUNT probe right after the service announces itself. */
void ext4fs_probe(void);

/* arena.c: first-fit allocator with coalescing free (long-lived service). */
void *arena_alloc(u32 bytes);
void arena_free(void *pointer);

#endif
