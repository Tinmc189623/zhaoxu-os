#ifndef ZHAOXU_PROCESS_H
#define ZHAOXU_PROCESS_H

#include <idt.h>
#include <types.h>

#define MAX_PROCESSES 16

struct thread;
struct address_space;

struct process_info {
    int active;
    u64 pid;
    u64 entry;
    u64 kernel_stack;
    u64 user_pages;
};

void process_init(void);
u64 process_spawn(const char *path, int argc, char **argv);
int process_wait(u64 pid, int *status);
int process_run(const char *path, int argc, char **argv);
int process_run_image(const u8 *image, u64 size, const char *name,
                      int argc, char **argv);
u64 process_self_pid(void);
struct address_space *process_current_space(void);
int process_active(void);
const struct process_info *process_get_info(void);
void process_thread_reaped(struct thread *thread);

void syscall_dispatch(struct interrupt_frame *frame);
void process_handle_exception(struct interrupt_frame *frame)
    __attribute__((noreturn));

#endif
