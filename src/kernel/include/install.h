#ifndef ZHAOXU_INSTALL_H
#define ZHAOXU_INSTALL_H

#include <types.h>

/* The installer is self-contained: stage 1/2 and the running kernel are
 * written to the target, and a FAT32 ESP is formatted in place at
 * INSTALL_PARTITION_LBA with the embedded UEFI loader and kernel file.
 * Both MBR and GPT layouts are supported. */
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

/* User-selected sizes.  `data_sectors` is 0 for the automatic two-partition
 * layout; otherwise a third FAT32 ZHAOXUDATA partition is created after the
 * ext4 root. */
struct install_layout {
    u32 esp_sectors;
    u64 data_sectors;
};

int install_scan(struct install_disk_info *out, int capacity);
int install_pick_target(void);
/* `progress` runs after each coarse write stage; `context` is passed through
 * so a syscall can update the caller's shared progress page.  `layout` may be
 * NULL for the default 64 MiB ESP plus the rest as ext4 root. */
int install_run(int target_disk, int gpt, const struct install_layout *layout,
                void (*progress)(void *context, u32 done, u32 total),
                void *context);
const char *install_last_error(void);

#endif
