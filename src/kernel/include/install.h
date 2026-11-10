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

int install_scan(struct install_disk_info *out, int capacity);
int install_pick_target(void);
int install_run(int target_disk, int gpt,
                void (*progress)(u32 done, u32 total));
const char *install_last_error(void);

#endif
