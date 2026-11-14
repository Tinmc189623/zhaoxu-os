/*
 * Zhaoxu OS - procfs.
 *
 * A synthetic view of scheduler, memory, timer, block and mount state.  It is
 * intentionally read-only and generated on demand; there is no proc inode
 * cache to invalidate when the kernel state changes.
 */
#include "synfs.h"

#include <block.h>
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <smp.h>
#include <string.h>
#include <timer.h>
#include <types.h>
#include <version.h>
#include <vfs.h>

static int proc_cpuinfo(char *buffer, usize capacity)
{
    return ksnprintf(buffer, capacity,
                     "processor : %u\n"
                     "timer_hz  : %u\n"
                     "architecture : x86_64\n",
                     smp_cpu_count(), timer_hz());
}

static int proc_meminfo(char *buffer, usize capacity)
{
    struct pmm_stats pmm;
    u64 heap_total = 0;
    u64 heap_used = 0;
    u64 heap_free = 0;
    pmm_get_stats(&pmm);
    heap_get_stats(&heap_total, &heap_used, &heap_free);
    return ksnprintf(buffer, capacity,
                     "MemTotal: %llu kB\n"
                     "MemFree:  %llu kB\n"
                     "HeapTotal: %llu kB\n"
                     "HeapUsed:  %llu kB\n"
                     "HeapFree:  %llu kB\n",
                     (pmm.free_pages + pmm.used_pages) * 4,
                     pmm.free_pages * 4,
                     heap_total >> 10, heap_used >> 10, heap_free >> 10);
}

static int proc_uptime(char *buffer, usize capacity)
{
    return ksnprintf(buffer, capacity, "%llu.%02llu %llu\n",
                     timer_uptime_seconds(),
                     (timer_ticks() % (timer_hz() ? timer_hz() : 1)) * 100 /
                         (timer_hz() ? timer_hz() : 1),
                     timer_ticks());
}

static int proc_version(char *buffer, usize capacity)
{
    return ksnprintf(buffer, capacity, "Zhaoxu OS %s (%s %s)\n",
                     ZHAOXU_VERSION, ZHAOXU_KERNEL, ZHAOXU_KERNEL_VERSION);
}

static int proc_mounts(char *buffer, usize capacity)
{
    int result = vfs_mounts_text(buffer, capacity);
    return result < 0 ? 0 : result;
}

static int proc_devices(char *buffer, usize capacity)
{
    int count = block_disk_count();
    usize used = 0;
    for (int i = 0; i < count && used + 1 < capacity; i++) {
        const char *model = block_disk_model(i);
        int written = ksnprintf(buffer + used, capacity - used,
                                "block %d: %s, %llu sectors\n",
                                i, model && model[0] ? model : "disk",
                                block_disk_sectors(i));
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

static int proc_self_status(char *buffer, usize capacity)
{
    const struct process_info *info = process_get_info();
    if (!info || !info->pid)
        return ksnprintf(buffer, capacity, "Pid:\t0\n");
    return ksnprintf(buffer, capacity,
                     "Pid:\t%llu\n"
                     "Active:\t%d\n"
                     "Entry:\t0x%llx\n"
                     "UserPages:\t%llu\n",
                     info->pid, info->active, info->entry,
                     info->user_pages);
}

static const struct synfs_entry g_proc_entries[] = {
    {"cpuinfo", 0, proc_cpuinfo},
    {"meminfo", 0, proc_meminfo},
    {"uptime", 0, proc_uptime},
    {"version", 0, proc_version},
    {"mounts", 0, proc_mounts},
    {"devices", 0, proc_devices},
    {"self", 1, NULL},
    {"self/status", 0, proc_self_status},
};

int procfs_mount(void)
{
    return synfs_mount("/proc", "procfs", g_proc_entries,
                       ARRAY_SIZE(g_proc_entries));
}
