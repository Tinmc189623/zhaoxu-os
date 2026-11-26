#ifndef ZHAOXU_SHM_H
#define ZHAOXU_SHM_H

#include <types.h>

#define SHM_MAX_OBJECTS 16
#define SHM_MAX_PAGES 2048

u64 shm_create(u64 size);
u64 yuanshuo_shm_map(u64 id);
int yuanshuo_shm_unmap(u64 id);
int shm_close(u64 id);
void shm_process_exit(u64 pid);

#endif
