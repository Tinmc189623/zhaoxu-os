#ifndef ZHAOXU_PROCESS_INTERNAL_H
#define ZHAOXU_PROCESS_INTERNAL_H

#include <process.h>
#include <types.h>
#include <zfd.h>
#include <zsignal.h>

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
    u64 flags;   /* page flags, so fork() can rebuild W^X exactly */
};

/* User register state a fork() child must resume with.  The layout is also
 * consumed by user_enter_context in arch/x86_64/user.S. */
struct user_context {
    u64 rbx;
    u64 rbp;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 rip;
    u64 rflags;
    u64 rsp;
};

struct process {
    int used;
    u64 pid;
    u64 parent_pid;
    int state;
    int exit_code;
    int thread_reaped;
    int block_cap;   /* trusted block I/O (syscalls 38..42) */
    int display_cap; /* trusted KMS scanout pages (/bin/display) */
    struct address_space *space;
    int mapping_count;
    int mapping_capacity;
    struct user_mapping *mappings;
    u64 entry;
    u64 user_stack;
    /* TLS base inherited by fork(); each thread keeps its own live copy. */
    u64 fs_base;
    /* Program break: the heap starts after the last loaded segment and may
     * grow up to brk_limit without colliding with the module window. */
    u64 brk_start;
    u64 brk_current;
    u64 brk_limit;
    /* Anonymous mappings live in a dedicated VA window and are tracked as
     * regions so munmap/mprotect do not scan the whole page list. */
#define ANON_MAX_REGIONS 128
    struct anon_region {
        u64 start;
        u64 end;
        u32 prot;
        u32 pad;
    } anon_regions[ANON_MAX_REGIONS];
    int anon_count;
    u64 anon_cursor;
    /* POSIX signal state.  One kernel thread per process keeps this flat. */
    struct zos_sigaction sig_actions[ZOS_NSIG];
    u64 sig_blocked;
    u64 sig_pending;
    struct zfd_entry fds[ZFD_MAX];
    /* POSIX stdio redirection: -1 means the default console/tty, otherwise
     * the zfd number that fd 0/1/2 resolves to after dup2. */
    int stdio[3];
};

int user_range_ok(struct process *process, u64 pointer, u64 length);
int copy_path_from_user(struct process *process, char *destination,
                        usize capacity, u64 source);

struct process *process_alloc(void);
struct process *process_lookup(u64 pid);
void process_release_pages(struct process *process);
u64 process_brk(struct process *process, u64 requested);
void process_unmap_page(struct process *process, u64 virtual_address);
long process_anon_mmap(struct process *process, u64 address, u64 length,
                       u32 prot, u32 flags);
long process_anon_munmap(struct process *process, u64 address, u64 length);
long process_anon_mprotect(struct process *process, u64 address, u64 length,
                           u32 prot);
long process_anon_madvise(struct process *process, u64 address, u64 length,
                          int advice);
int process_prepare_image(struct process *process, const u8 *image,
                          u64 image_size, const char *path, int argc,
                          const char **argv);

u64 process_map_page(struct process *process, u64 virtual_address,
                     u64 page_flags);
int zxe_load_image(struct process *process, const u8 *image, u64 image_size,
                   const char *path, u64 *entry_out);
int process_user_write(struct process *process, u64 virtual_address,
                        const void *source, u64 length);
struct process *process_current(void);
void process_exit_current(int status) __attribute__((noreturn));

#endif
