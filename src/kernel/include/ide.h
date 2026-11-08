#ifndef ZHAOXU_IDE_H
#define ZHAOXU_IDE_H

#include <types.h>

/* Implemented in Ada (src/drivers/ide.adb). */
int ide_init(void);
int ide_drive_count(void);
int ide_drive_atapi(int drive);
int ide_drive_bus(int drive);
int ide_drive_unit(int drive);
u64 ide_drive_sectors(int drive);
const char *ide_drive_model(int drive);
int ide_drive_read(int drive, u64 lba, u32 count, void *buffer);
int ide_atapi_packet(int drive, const void *cdb, u32 cdb_len,
                     void *data, u32 data_len);

#endif
