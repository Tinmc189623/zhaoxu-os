#ifndef ZHAOXU_AHCI_H
#define ZHAOXU_AHCI_H

#include <types.h>

int ahci_init(void);
int ahci_disk_count(void);
u64 ahci_disk_sectors(int disk);
int ahci_disk_port(int disk);
u64 ahci_disk_sectors(int disk);
int ahci_disk_read(int disk, u64 lba, u32 count, void *buffer);
int ahci_disk_write(int disk, u64 lba, u32 count, const void *buffer);
int ahci_disk_flush(int disk);
int ahci_disk_port(int disk);
const char *ahci_disk_model(int disk);

#endif
