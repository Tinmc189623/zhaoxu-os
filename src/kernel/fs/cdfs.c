/*
 * Zhaoxu OS - CDFS (CD-ROM filesystem) probe.
 *
 * CDFS is ISO 9660 carried on an optical track.  A CD can present three
 * different sector layouts depending on the track mode, so this file only
 * locates the volume descriptor and hands the parser an adjusted volume:
 *
 *   2048-byte mode 1 data track       logical 2048, offset 0
 *   2352-byte mode 1 raw sectors      logical 2048, stride 2352, offset 16
 *   2352-byte mode 2 form 1 raw       logical 2048, stride 2352, offset 24
 *   2336-byte mode 2 form 1 sectors   logical 2048, stride 2336, offset 8
 */
#include <blockfs.h>
#include <kprintf.h>
#include <mem.h>
#include <string.h>
#include <types.h>
#include <vfs.h>

int iso9660_mount_as(const struct blockfs_volume *volume, const char *path,
                     const char *type);

struct cdf_layout {
    u32 stride;
    u32 offset;
};

static const struct cdf_layout g_layouts[] = {
    {2048, 0},
    {2352, 16},
    {2352, 24},
    {2336, 8},
};

/* Test one sector layout by looking for the primary volume descriptor at
 * logical sector 16. */
static int cdf_try_layout(const struct blockfs_volume *volume,
                          const struct cdf_layout *layout)
{
    struct blockfs_volume probe = *volume;
    probe.logical = 2048;
    probe.stride = layout->stride;
    probe.data_offset = layout->offset;

    u8 block[8];
    if (bf_read_at(&probe, 16ull * 2048 + 1, block, sizeof(block)) != 0)
        return -1;
    if (memcmp(block, "CD001", 5) != 0)
        return -1;
    return 0;
}

int cdfs_probe(const struct blockfs_volume *volume)
{
    if (!volume)
        return -1;
    for (u32 i = 0; i < sizeof(g_layouts) / sizeof(g_layouts[0]); i++) {
        if (cdf_try_layout(volume, &g_layouts[i]) == 0)
            return 0;
    }
    return -1;
}

int cdfs_mount_volume(const struct blockfs_volume *volume, const char *path)
{
    if (!volume || !path)
        return -1;
    for (u32 i = 0; i < sizeof(g_layouts) / sizeof(g_layouts[0]); i++) {
        if (cdf_try_layout(volume, &g_layouts[i]) != 0)
            continue;
        struct blockfs_volume adjusted = *volume;
        adjusted.logical = 2048;
        adjusted.stride = g_layouts[i].stride;
        adjusted.data_offset = g_layouts[i].offset;
        return iso9660_mount_as(&adjusted, path, "cdfs");
    }
    return -1;
}
