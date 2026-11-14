/*
 * Zhaoxu OS - removable media probe.
 *
 * The boot data volume is mounted at /mnt by the storage adapter.  Every
 * other filesystem found on the disks is mounted under /media/<n> so that
 * NTFS, exFAT, VFAT, ISO 9660/CDFS, UDF and UBIFS volumes are reachable
 * without a mount(8) daemon.  The probe order is signature-specific first
 * (NTFS, exFAT), then the more permissive FAT boot-sector test, then the
 * optical and flash formats.
 */
#include <block.h>
#include <blockfs.h>
#include <kprintf.h>
#include <types.h>
#include <vfs.h>

#define MEDIA_MAX_VOLUMES 32

struct media_driver {
    const char *name;
    int (*probe)(const struct blockfs_volume *volume);
    int (*mount)(const struct blockfs_volume *volume, const char *path);
};

static const struct media_driver g_drivers[] = {
    {"ntfs", ntfs_probe, ntfs_mount_volume},
    {"exfat", exfat_probe, exfat_mount_volume},
    {"vfat", vfat_probe, vfat_mount_volume},
    {"iso9660", iso9660_probe, iso9660_mount_volume},
    {"cdfs", cdfs_probe, cdfs_mount_volume},
    {"udf", udf_probe, udf_mount_volume},
    {"ubifs", ubifs_probe, ubifs_mount_volume},
};

/* Try every driver until one claims the volume and mounts it. */
static int mount_volume(const struct blockfs_volume *volume, const char *path)
{
    for (u32 i = 0; i < sizeof(g_drivers) / sizeof(g_drivers[0]); i++) {
        const struct media_driver *driver = &g_drivers[i];
        if (vfs_mount_count() >= VFS_MAX_MOUNTS - 1)
            return -1;
        if (driver->probe(volume) != 0)
            continue;
        if (driver->mount(volume, path) == 0) {
            kprintf("[media] disk %d LBA %llu -> %s (%s)\n",
                    volume->disk, volume->base, path, driver->name);
            return 0;
        }
    }
    return -1;
}

void media_probe_all(void)
{
    int disks = block_disk_count();
    if (disks <= 0)
        return;

    int mounted = 0;
    for (int disk = 0; disk < disks; disk++) {
        struct blockfs_volume volumes[MEDIA_MAX_VOLUMES];
        int count = bf_scan_volumes(disk, volumes, MEDIA_MAX_VOLUMES);
        for (int i = 0; i < count; i++) {
            if (vfs_mount_count() >= VFS_MAX_MOUNTS - 1)
                return;
            char path[32];
            int written = ksnprintf(path, sizeof(path), "/media/%d",
                                    mounted);
            if (written <= 0 || (usize)written >= sizeof(path))
                return;
            if (mount_volume(&volumes[i], path) == 0)
                mounted++;
        }
    }
    if (!mounted)
        kprintf("[media] no additional filesystems found\n");
}
