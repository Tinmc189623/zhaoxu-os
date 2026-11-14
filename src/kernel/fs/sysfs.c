/*
 * Zhaoxu OS - sysfs.
 *
 * A compact device/kernel tree.  Entries are generated from the live block
 * and CPU state; mount points such as /sys/firmware/efi/efivars are emitted
 * by the VFS itself when a directory containing them is listed.
 */
#include "synfs.h"

#include <block.h>
#include <dev.h>
#include <kprintf.h>
#include <mem.h>
#include <smp.h>
#include <string.h>
#include <types.h>
#include <version.h>

static int sys_version(char *buffer, usize capacity)
{
    return ksnprintf(buffer, capacity, "%s %s build %s\n", ZHAOXU_VERSION,
                     ZHAOXU_KERNEL_VERSION, ZHAOXU_BUILD);
}

static int sys_cpus(char *buffer, usize capacity)
{
    return ksnprintf(buffer, capacity, "%u\n", smp_cpu_count());
}

static int sys_heap(char *buffer, usize capacity)
{
    u64 total = 0;
    u64 used = 0;
    u64 free_bytes = 0;
    heap_get_stats(&total, &used, &free_bytes);
    return ksnprintf(buffer, capacity,
                     "total %llu\nused %llu\nfree %llu\n",
                     total, used, free_bytes);
}

static int sys_block_devices(char *buffer, usize capacity)
{
    int count = block_disk_count();
    usize used = 0;
    for (int i = 0; i < count && used + 1 < capacity; i++) {
        const char *model = block_disk_model(i);
        int written = ksnprintf(buffer + used, capacity - used,
                                "%d %s %llu\n", i,
                                model && model[0] ? model : "disk",
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

static int sys_cpu_online(char *buffer, usize capacity)
{
    u32 count = smp_cpu_count();
    if (!count)
        return ksnprintf(buffer, capacity, "\n");
    return ksnprintf(buffer, capacity, "0-%u\n", count - 1);
}

/* Millidegrees Celsius, matching the Linux thermal ABI. */
static int sys_thermal_temp(char *buffer, usize capacity)
{
    if (!dev_sensor_ready())
        return ksnprintf(buffer, capacity, "0\n");
    return ksnprintf(buffer, capacity, "%u\n", dev_sensor_temp());
}

static const struct synfs_entry g_sys_entries[] = {
    {"kernel", 1, NULL},
    {"kernel/version", 0, sys_version},
    {"kernel/cpus", 0, sys_cpus},
    {"kernel/heap", 0, sys_heap},
    {"class", 1, NULL},
    {"class/block", 1, NULL},
    {"class/block/devices", 0, sys_block_devices},
    {"class/thermal", 1, NULL},
    {"class/thermal/thermal_zone0", 1, NULL},
    {"class/thermal/thermal_zone0/temp", 0, sys_thermal_temp},
    {"devices", 1, NULL},
    {"devices/system", 1, NULL},
    {"devices/system/cpu", 1, NULL},
    {"devices/system/cpu/online", 0, sys_cpu_online},
    {"firmware", 1, NULL},
    {"firmware/efi", 1, NULL},
};

int sysfs_mount(void)
{
    return synfs_mount("/sys", "sysfs", g_sys_entries,
                       ARRAY_SIZE(g_sys_entries));
}
