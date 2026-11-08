#ifndef ZHAOXU_SCSI_H
#define ZHAOXU_SCSI_H

#include <types.h>

/* Implemented in Ada (src/drivers/scsi.adb), talking ATAPI through IDE. */
int scsi_init(void);
int scsi_device_count(void);
const char *scsi_vendor(int dev);
const char *scsi_product(int dev);
const char *scsi_revision(int dev);
u64 scsi_capacity(int dev);
int scsi_read(int dev, u64 lba, u32 count, void *buffer);

#endif
