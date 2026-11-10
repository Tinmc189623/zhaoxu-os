#ifndef ZHAOXU_FAT32_INTERNAL_H
#define ZHAOXU_FAT32_INTERNAL_H

#include <fat32.h>
#include <types.h>

#define FAT32_LABEL_MAX 12

struct fat32_fs {
    int disk;
    u64 volume_base;
    u32 bytes_per_sector;
    u32 sectors_per_cluster;
    u32 reserved_sectors;
    u32 num_fats;
    u32 fat_size;
    u32 root_cluster;
    u32 total_sectors;
    u32 data_start;
    u32 cluster_count;
    u32 fsinfo_sector;
    char label[FAT32_LABEL_MAX];
    int mounted;
    u32 cache_sector;
    int cache_valid;
    u8 cache[512];
};

struct fat_entry {
    char name[256];
    u32 cluster;
    u32 size;
    int is_dir;
};

struct fat32_fs *fat32_state(void);

u16 read_le16(const u8 *p);
u32 read_le32(const u8 *p);
int read_sector(u64 lba, void *buffer);
int read_cluster(u32 cluster, void *buffer);
u32 fat_next_cluster(u32 cluster);
int name_equal(const char *a, const char *b);
void short_name(const u8 *entry, char *output);
int fat32_find(const char *path, struct fat_entry *result);
void fat32_alloc_reset(void);

#endif
