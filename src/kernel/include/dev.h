#ifndef ZHAOXU_DEV_H
#define ZHAOXU_DEV_H

#include <types.h>

struct rtc_time;

#define DEV_MAGIC 0x5A4F5344u          /* "ZOSD" */

enum dev_state {
    DEV_STATE_IDLE = 0,
    DEV_STATE_REQUEST = 1,
    DEV_STATE_DONE = 2,
    DEV_STATE_READY = 3
};

enum dev_op {
    DEV_OP_NONE = 0,
    DEV_OP_READ = 1,
    DEV_OP_WRITE = 2,
    DEV_OP_FLUSH = 3,
    DEV_OP_INFO = 4,
    DEV_OP_RTC = 5
};

struct dev_header {
    volatile u32 magic;
    volatile u32 state;
    u32 op;
    i32 status;
    u32 disk;
    u32 count;
    u64 lba;
    u64 data_phys;
    u64 input_phys;
    u64 log_phys;
    u64 rsdp;
    u32 width;
    u32 height;
    u32 disk_count;
    u32 ports[8];
    u64 sectors[8];
    char models[8][41];
};

struct dev_input {
    volatile u32 head;
    volatile u32 tail;
    char keys[512];
    volatile i32 mouse_x;
    volatile i32 mouse_y;
    volatile u32 mouse_buttons;
    volatile u32 mouse_ready;
};

#define DEV_LOG_SIZE 4088
struct dev_log {
    volatile u32 head;
    volatile u32 tail;
    char data[DEV_LOG_SIZE];
};

void dev_memory_init(void);
int dev_start(void);
int dev_ready(void);
void dev_log_write(const char *text, usize length);

int dev_disk_count(void);
u64 dev_disk_sectors(int disk);
int dev_disk_port(int disk);
const char *dev_disk_model(int disk);
int dev_disk_read(int disk, u64 lba, u32 count, void *buffer);
int dev_disk_write(int disk, u64 lba, u32 count, const void *buffer);
int dev_disk_flush(int disk);

int dev_rtc_read(struct rtc_time *time);
int dev_key_pop(void);
int dev_key_has_input(void);
void dev_mouse_state(u64 state[3], int *ready);
u64 dev_alloc_page(void);

#endif
