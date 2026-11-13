#ifndef ZHAOXU_PROCESS_INTERNAL_H
#define ZHAOXU_PROCESS_INTERNAL_H

#include <process.h>
#include <types.h>

struct address_space;

#define USER_MAX_PAGES 65536
#define USER_MAP_INITIAL_CAPACITY 64

enum process_state {
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_ZOMBIE
};

struct user_mapping {
    u64 virtual_address;
    u64 physical_address;
};

struct process {
    int used;
    u64 pid;
    u64 parent_pid;
    int state;
    int exit_code;
    int thread_reaped;
    int block_cap;   /* trusted block I/O (syscalls 38..42) */
    struct address_space *space;
    int mapping_count;
    int mapping_capacity;
    struct user_mapping *mappings;
    u64 entry;
    u64 user_stack;
};

u64 process_map_page(struct process *process, u64 virtual_address,
                     u64 page_flags);
int zxe_load_image(struct process *process, const u8 *image, u64 image_size,
                   const char *path, u64 *entry_out);
int process_user_write(struct process *process, u64 virtual_address,
                        const void *source, u64 length);
struct process *process_current(void);
void process_exit_current(int status) __attribute__((noreturn));

#endif
