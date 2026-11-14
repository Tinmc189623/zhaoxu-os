#ifndef ZHAOXU_POLL_H
#define ZHAOXU_POLL_H

/* Linux-compatible poll event bits and layout (x86_64). */
#define ZPOLLIN   0x001
#define ZPOLLOUT  0x004
#define ZPOLLERR  0x008
#define ZPOLLHUP  0x010
#define ZPOLLNVAL 0x020

struct zpollfd {
    int fd;
    short events;
    short revents;
};

#endif
