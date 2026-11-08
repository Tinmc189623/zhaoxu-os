#ifndef ZHAOXU_PROCESS_H
#define ZHAOXU_PROCESS_H

#include <idt.h>
#include <types.h>

struct process_info {
    int active;
    u64 pid;
    u64 entry;
    u64 kernel_stack;
    u64 user_pages;
};

void process_init(void);
int process_run(const char *path, int argc, char **argv);
int process_active(void);
const struct process_info *process_get_info(void);

void syscall_dispatch(struct interrupt_frame *frame);
void process_handle_exception(struct interrupt_frame *frame)
    __attribute__((noreturn));

#endif
