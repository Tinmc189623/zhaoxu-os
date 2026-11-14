#ifndef ZHAOXU_EPOLL_H
#define ZHAOXU_EPOLL_H

#include <types.h>

#define EPOLL_MAX_ENTRIES 32

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLIN  0x001u
#define EPOLLOUT 0x004u
#define EPOLLERR 0x008u
#define EPOLLHUP 0x010u

struct process;

/* Result layout matches the packed Linux x86_64 epoll_event (12 bytes). */
struct epoll_result {
    u32 events;
    u64 data;
} __attribute__((packed));

struct epoll;

struct epoll *epoll_create(void);
void epoll_ref(struct epoll *epoll);
void epoll_release(struct epoll *epoll);
int epoll_ctl(struct process *process, struct epoll *epoll, int op, int fd,
              u32 events, u64 data);
int epoll_wait(struct process *process, struct epoll *epoll,
               struct epoll_result *results, int max_events, int timeout_ms);

#endif
