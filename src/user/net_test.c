/*
 * Zhaoxu OS - M0 transport self-test.
 *
 * Exercises the whole Wayland prerequisite set without the compositor:
 * AF_UNIX stream sockets, poll readiness, memfd + mmap, SCM_RIGHTS fd
 * passing and O_NONBLOCK.  Run it from 8sh with `run /data/NET_TEST.ZXE`
 * once the toolchain can produce a bootable ISO.
 */
#include "userlib.h"

static int fail(const char *where)
{
    u_puts("[m0] FAIL: ");
    u_puts(where);
    u_puts("\n");
    return 1;
}

static int compare(const char *a, const char *b, int length)
{
    for (int i = 0; i < length; i++) {
        if (a[i] != b[i])
            return -1;
    }
    return 0;
}

int user_main(long *stack)
{
    (void)stack;

    int pair[2];
    if (sys_socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
        return fail("socketpair");

    static const char payload[] = "wayland-m0";
    if (sys_write(pair[0], payload, 10) != 10)
        return fail("write");

    struct pollfd ready = { pair[1], POLLIN, 0 };
    if (sys_poll(&ready, 1, 1000) != 1 || !(ready.revents & POLLIN))
        return fail("poll");

    char received[16];
    long got = sys_read(pair[1], received, sizeof(received));
    if (got != 10 || compare(received, payload, 10) != 0)
        return fail("read");

    long fd = sys_memfd_create("m0-pool", 0);
    if (fd < 0)
        return fail("memfd_create");
    if (sys_ftruncate(fd, 4096) != 0)
        return fail("ftruncate");

    struct zos_mmap_args mapping;
    mapping.address = 0;
    mapping.length = 4096;
    mapping.protection = 3;
    mapping.flags = 1;
    mapping.fd = fd;
    mapping.offset = 0;
    long address = sys_mmap(&mapping);
    if (address < 0)
        return fail("mmap");
    char *pool = (char *)address;
    pool[0] = 'Z';
    pool[1] = 'X';
    pool[2] = 'E';
    pool[3] = 0;

    struct iovec send_iov;
    send_iov.iov_base = (void *)"fd";
    send_iov.iov_len = 2;
    unsigned char send_control[CMSG_SPACE(sizeof(int))];
    for (unsigned long i = 0; i < sizeof(send_control); i++)
        send_control[i] = 0;
    struct msghdr send_header;
    for (unsigned long i = 0; i < sizeof(send_header); i++)
        ((unsigned char *)&send_header)[i] = 0;
    send_header.msg_iov = &send_iov;
    send_header.msg_iovlen = 1;
    send_header.msg_control = send_control;
    send_header.msg_controllen = sizeof(send_control);
    struct cmsghdr *cmsg = (struct cmsghdr *)send_control;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    *(int *)CMSG_DATA(cmsg) = (int)fd;
    if (sys_sendmsg(pair[0], &send_header, 0) != 2)
        return fail("sendmsg");

    char small[4];
    unsigned char recv_control[64];
    for (unsigned long i = 0; i < sizeof(recv_control); i++)
        recv_control[i] = 0;
    struct msghdr recv_header;
    for (unsigned long i = 0; i < sizeof(recv_header); i++)
        ((unsigned char *)&recv_header)[i] = 0;
    struct iovec recv_iov;
    recv_iov.iov_base = small;
    recv_iov.iov_len = sizeof(small);
    recv_header.msg_iov = &recv_iov;
    recv_header.msg_iovlen = 1;
    recv_header.msg_control = recv_control;
    recv_header.msg_controllen = sizeof(recv_control);
    if (sys_recvmsg(pair[1], &recv_header, 0) != 2)
        return fail("recvmsg");
    if (recv_header.msg_controllen < CMSG_LEN(sizeof(int)))
        return fail("recvmsg control");
    struct cmsghdr *received_cmsg = (struct cmsghdr *)recv_control;
    if (received_cmsg->cmsg_level != SOL_SOCKET ||
        received_cmsg->cmsg_type != SCM_RIGHTS)
        return fail("scm rights");
    int passed_fd = *(int *)CMSG_DATA(received_cmsg);
    if (passed_fd < 64 || passed_fd == fd)
        return fail("passed fd");

    struct zos_mmap_args second;
    second.address = 0;
    second.length = 4096;
    second.protection = 3;
    second.flags = 1;
    second.fd = passed_fd;
    second.offset = 0;
    long second_address = sys_mmap(&second);
    if (second_address < 0)
        return fail("mmap passed fd");
    const char *shared = (const char *)second_address;
    if (shared[0] != 'Z' || shared[1] != 'X' || shared[2] != 'E')
        return fail("shared pages");

    if (sys_fcntl(pair[1], F_SETFL, O_NONBLOCK) != 0)
        return fail("fcntl");
    if (sys_read(pair[1], received, 1) != -1)
        return fail("nonblock");

    sys_munmap((void *)address, 4096);
    sys_munmap((void *)second_address, 4096);
    sys_close(fd);
    sys_close(passed_fd);
    sys_close(pair[0]);
    sys_close(pair[1]);

    u_puts("[m0] socket, poll, memfd, mmap, SCM_RIGHTS PASS\n");
    return 0;
}
