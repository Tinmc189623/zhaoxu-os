/*
 * Zhaoxu OS - debugfs.
 *
 * Read-only diagnostics that are cheap to regenerate and safe to expose to
 * an unprivileged process in this single-user system.
 */
#include "synfs.h"

#include <block.h>
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <smp.h>
#include <timer.h>
#include <types.h>
#include <vfs.h>

static int debug_mem(char *buffer, usize capacity)
{
    struct pmm_stats pmm;
    u64 heap_total = 0;
    u64 heap_used = 0;
    u64 heap_free = 0;
    pmm_get_stats(&pmm);
    heap_get_stats(&heap_total, &heap_used, &heap_free);
    return ksnprintf(buffer, capacity,
                     "pmm managed %llu pages\n"
                     "pmm free    %llu pages\n"
                     "heap total  %llu bytes\n"
                     "heap used   %llu bytes\n"
                     "heap free   %llu bytes\n",
                     pmm.free_pages + pmm.used_pages,
                     pmm.free_pages, heap_total, heap_used, heap_free);
}

static int debug_vfs(char *buffer, usize capacity)
{
    return ksnprintf(buffer, capacity, "mounts %d\n", vfs_mount_count());
}

static int debug_mounts(char *buffer, usize capacity)
{
    int result = vfs_mounts_text(buffer, capacity);
    return result < 0 ? 0 : result;
}

static int debug_sched(char *buffer, usize capacity)
{
    const struct process_info *info = process_get_info();
    return ksnprintf(buffer, capacity,
                     "cpus %u\n"
                     "ticks %llu\n"
                     "pid %llu\n"
                     "active %d\n"
                     "user_pages %llu\n",
                     smp_cpu_count(), timer_ticks(),
                     info ? info->pid : 0,
                     info ? info->active : 0,
                     info ? info->user_pages : 0);
}

static int debug_block(char *buffer, usize capacity)
{
    int count = block_disk_count();
    usize used = 0;
    for (int i = 0; i < count && used + 1 < capacity; i++) {
        const char *model = block_disk_model(i);
        int written = ksnprintf(buffer + used, capacity - used,
                                "disk %d sectors %llu model %s\n", i,
                                block_disk_sectors(i),
                                model && model[0] ? model : "disk");
        if (written <= 0)
            break;
        if ((usize)written >= capacity - used) {
            used = capacity - 1;
            break;
        }
        used += (usize)written;
    }
    if (used < capacity)
        buffer[used] = 0;
    return (int)used;
}

static const struct synfs_entry g_debug_entries[] = {
    {"mem", 0, debug_mem},
    {"vfs", 0, debug_vfs},
    {"mounts", 0, debug_mounts},
    {"sched", 0, debug_sched},
    {"block", 0, debug_block},
};

int debugfs_mount(void)
{
    return synfs_mount("/debug", "debugfs", g_debug_entries,
                       ARRAY_SIZE(g_debug_entries));
}
