#ifndef ZHAOXU_EXT4_ENV_H
#define ZHAOXU_EXT4_ENV_H

#include <types.h>

struct ext4_env {
    int (*read)(int disk, u64 base, u64 offset, void *buffer, u32 length);
    int (*write)(int disk, u64 base, u64 offset, const void *buffer,
                 u32 length);
    int (*flush)(int disk);
    void *(*alloc)(u32 bytes);
    void (*free)(void *pointer);
    void (*log)(const char *text);
    int (*disk_count)(void);
    u64 (*disk_sectors)(int disk);
};

void ext4_env_set(const struct ext4_env *env);
const struct ext4_env *ext4_env(void);
void *ext4_alloc(u32 bytes);
void ext4_free(void *pointer);
void ext4_logf(const char *format, ...);

#endif
