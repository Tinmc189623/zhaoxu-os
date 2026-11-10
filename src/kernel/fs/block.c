/* Zhaoxu OS - block device facade over the Ring3 driver service. */
#include <block.h>
#include <dev.h>
#include <types.h>

int block_disk_count(void)
{
    return dev_disk_count();
}

u64 block_disk_sectors(int disk)
{
    return dev_disk_sectors(disk);
}

int block_disk_port(int disk)
{
    return dev_disk_port(disk);
}

const char *block_disk_model(int disk)
{
    return dev_disk_model(disk);
}

int block_disk_read(int disk, u64 lba, u32 count, void *buffer)
{
    return dev_disk_read(disk, lba, count, buffer);
}

int block_disk_write(int disk, u64 lba, u32 count, const void *buffer)
{
    return dev_disk_write(disk, lba, count, buffer);
}

int block_disk_flush(int disk)
{
    return dev_disk_flush(disk);
}
