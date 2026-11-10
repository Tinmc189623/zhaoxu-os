#ifndef ZHAOXU_BLOCK_H
#define ZHAOXU_BLOCK_H

#include <types.h>

int block_disk_count(void);
u64 block_disk_sectors(int disk);
int block_disk_port(int disk);
const char *block_disk_model(int disk);
int block_disk_read(int disk, u64 lba, u32 count, void *buffer);
int block_disk_write(int disk, u64 lba, u32 count, const void *buffer);
int block_disk_flush(int disk);

#endif
