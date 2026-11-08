#ifndef ZHAOXU_INSTALL_H
#define ZHAOXU_INSTALL_H

#include <types.h>

/* The installed disk is dual-boot: the MBR holds the legacy BIOS stage 1
 * (ZhaoxuBootloader), stage 2 and the raw kernel follow it in the gap, and
 * the FAT16 ESP image (out/efi.img, label ZHAOXUEFI) is copied into a single
 * partition starting at INSTALL_PARTITION_LBA for UEFI firmware. */
#define INSTALL_PARTITION_LBA 2048

struct install_disk_info {
    int disk;
    int port;
    u64 sectors;
    char model[41];
    char label[12];
    int is_esp;
    int is_data;
    int is_target;
};

int install_scan(struct install_disk_info *out, int capacity);
int install_source_disk(void);
u64 install_source_sectors(void);
int install_pick_target(int source_disk);
int install_run(int source_disk, int target_disk,
                void (*progress)(u32 done, u32 total));
const char *install_last_error(void);

#endif
