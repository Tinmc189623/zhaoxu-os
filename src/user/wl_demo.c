/*
 * Zhaoxu OS - minimal Wayland shm client (M2).
 *
 * Connects to /run/wayland-0 over the M0 AF_UNIX transport, binds
 * wl_compositor and wl_shm from wl_registry, creates a memfd-backed
 * wl_shm_pool, commits one wl_surface frame and waits for wl_callback.done.
 */
#include <types.h>
#include "userlib.h"

#define WIDTH 320u
#define HEIGHT 240u
#define STRIDE (WIDTH * 4u)
#define POOL_BYTES (STRIDE * HEIGHT)

static u32 str_len(const char *text)
{
    u32 length = 0;
    while (text[length])
        length++;
    return length;
}

static int str_equal(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void copy_bytes(void *destination, const void *source, u32 length)
{
    u8 *dst = destination;
    const u8 *src = source;
    for (u32 i = 0; i < length; i++)
        dst[i] = src[i];
}

static void zero_bytes(void *destination, u32 length)
{
    u8 *dst = destination;
    for (u32 i = 0; i < length; i++)
        dst[i] = 0;
}

static u32 put_header(u8 *buffer, u32 id, u16 opcode)
{
    *(u32 *)buffer = id;
    *(u16 *)(buffer + 4) = opcode;
    *(u16 *)(buffer + 6) = 0;
    return 8;
}

static u32 put_u32(u8 *buffer, u32 at, u32 value)
{
    *(u32 *)(buffer + at) = value;
    return at + 4;
}

static u32 put_string(u8 *buffer, u32 at, const char *text)
{
    u32 length = str_len(text) + 1;
    u32 padded = (length + 3u) & ~3u;
    put_u32(buffer, at, length);
    at += 4;
    copy_bytes(buffer + at, text, length);
    for (u32 i = length; i < padded; i++)
        buffer[at + i] = 0;
    return at + padded;
}

static int send_plain(int socket, u8 *buffer, u32 size)
{
    *(u16 *)(buffer + 6) = (u16)size;
    return sys_write(socket, buffer, size) == (long)size ? 0 : -1;
}

static int send_with_fd(int socket, u8 *buffer, u32 size, int fd)
{
    *(u16 *)(buffer + 6) = (u16)size;
    struct iovec iov;
    iov.iov_base = buffer;
    iov.iov_len = size;
    unsigned char control[CMSG_SPACE(sizeof(int))];
    zero_bytes(control, sizeof(control));
    struct msghdr message;
    zero_bytes(&message, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    struct cmsghdr *cmsg = (struct cmsghdr *)control;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    *(int *)CMSG_DATA(cmsg) = fd;
    return sys_sendmsg(socket, &message, 0) == (long)size ? 0 : -1;
}

static int read_global(const u8 *message, u32 size, u32 *name, char *interface,
                       u32 *version)
{
    if (size < 20)
        return -1;
    *name = *(const u32 *)(message + 8);
    u32 length = *(const u32 *)(message + 12);
    if (!length || length > 64 || 16 + length > size)
        return -1;
    copy_bytes(interface, message + 16, length);
    interface[length - 1] = 0;
    u32 at = 16 + ((length + 3u) & ~3u);
    if (at + 4 > size)
        return -1;
    *version = *(const u32 *)(message + at);
    return 0;
}

int user_main(long *stack)
{
    (void)stack;

    int socket = (int)sys_socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket < 0)
        return 1;
    struct sockaddr_un address;
    zero_bytes(&address, sizeof(address));
    address.sun_family = AF_UNIX;
    u32 path_length = str_len("/run/wayland-0") + 1;
    copy_bytes(address.sun_path, "/run/wayland-0", path_length);
    if (sys_connect(socket, &address, 2 + path_length) != 0) {
        u_puts("[wldemo] connect failed\n");
        return 1;
    }
    u_puts("[wldemo] connected\n");

    u8 buffer[4096];
    u32 at = put_header(buffer, 1, 1); /* wl_display.get_registry */
    at = put_u32(buffer, at, 2);
    if (send_plain(socket, buffer, at) != 0)
        return 1;

    long pool_fd = sys_memfd_create("wl-shm", 0);
    if (pool_fd < 0 || sys_ftruncate(pool_fd, POOL_BYTES) != 0)
        return 1;
    struct zos_mmap_args args;
    args.address = 0;
    args.length = POOL_BYTES;
    args.protection = 3;
    args.flags = 1;
    args.fd = pool_fd;
    args.offset = 0;
    long mapped = sys_mmap(&args);
    if (mapped < 0)
        return 1;
    u32 *pixels = (u32 *)(uintptr_t)mapped;
    for (u32 i = 0; i < WIDTH * HEIGHT; i++)
        pixels[i] = 0xFF3366CCu;

    int compositor = -1;
    int shm = -1;
    int xdg = -1;
    int seat = -1;
    int setup_sent = 0;
    int configured = 0;
    int frame_sent = 0;
    u8 input[4096];
    u32 have = 0;
    int done = 0;
    while (!done) {
        struct iovec iov;
        iov.iov_base = input + have;
        iov.iov_len = sizeof(input) - have;
        u8 control[64];
        zero_bytes(control, sizeof(control));
        struct msghdr message;
        zero_bytes(&message, sizeof(message));
        message.msg_iov = &iov;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        long length = sys_recvmsg(socket, &message, 0);
        if (length <= 0)
            break;
        if (message.msg_controllen >= sizeof(struct cmsghdr)) {
            struct cmsghdr *cmsg = (struct cmsghdr *)control;
            if (cmsg->cmsg_level == SOL_SOCKET &&
                cmsg->cmsg_type == SCM_RIGHTS)
                sys_close(*(int *)CMSG_DATA(cmsg));
        }
        have += (u32)length;
        u32 offset = 0;
        while (have - offset >= 8) {
            u32 id = *(const u32 *)(input + offset);
            u16 opcode = *(const u16 *)(input + offset + 4);
            u16 size = *(const u16 *)(input + offset + 6);
            if (size < 8 || size > have - offset)
                break;
            if (id == 2 && opcode == 0) {
                u32 name = 0;
                u32 version = 0;
                char interface[64];
                if (read_global(input + offset, size, &name, interface,
                                &version) == 0) {
                    u32 new_id = 0;
                    u32 message_at = put_header(buffer, 2, 0);
                    message_at = put_u32(buffer, message_at, name);
                    message_at = put_string(buffer, message_at, interface);
                    message_at = put_u32(buffer, message_at, version);
                    if (str_equal(interface, "wl_compositor"))
                        new_id = 3;
                    else if (str_equal(interface, "wl_shm"))
                        new_id = 4;
                    else if (str_equal(interface, "xdg_wm_base"))
                        new_id = 9;
                    else if (str_equal(interface, "wl_seat"))
                        new_id = 12;
                    message_at = put_u32(buffer, message_at, new_id);
                    send_plain(socket, buffer, message_at);
                    if (str_equal(interface, "wl_compositor"))
                        compositor = (int)new_id;
                    else if (str_equal(interface, "wl_shm"))
                        shm = (int)new_id;
                    else if (str_equal(interface, "xdg_wm_base"))
                        xdg = (int)new_id;
                    else if (str_equal(interface, "wl_seat"))
                        seat = (int)new_id;
                }
            } else if (id == 10 && opcode == 0) {
                u32 serial = *(const u32 *)(input + offset + 8);
                u32 ack_at = put_header(buffer, 10, 4);
                ack_at = put_u32(buffer, ack_at, serial);
                send_plain(socket, buffer, ack_at);
                configured = 1;
            } else if (id == 13 && opcode == 0) {
                u_puts("[wldemo] pointer enter\n");
            } else if (id == 13 && opcode == 2) {
                u_puts("[wldemo] pointer motion\n");
            } else if (id == 13 && opcode == 3) {
                u_puts("[wldemo] pointer button\n");
            } else if (id == 14 && opcode == 0) {
                u_puts("[wldemo] keyboard keymap\n");
            } else if (id == 14 && opcode == 1) {
                u_puts("[wldemo] keyboard enter\n");
            } else if (id == 14 && opcode == 3) {
                u_puts("[wldemo] keyboard key\n");
            } else if (id == 11 && opcode == 1) {
                u_puts("[wldemo] close requested\n");
                done = 1;
            } else if (id == 6 && opcode == 0) {
                done = 1;
            }
            offset += size;
        }
        if (offset) {
            for (u32 i = offset; i < have; i++)
                input[i - offset] = input[i];
            have -= offset;
        }
        if (!setup_sent && compositor >= 0 && shm >= 0 && xdg >= 0 &&
            seat >= 0) {
            u32 message_at = put_header(buffer, (u32)compositor, 0);
            message_at = put_u32(buffer, message_at, 5);
            send_plain(socket, buffer, message_at);

            message_at = put_header(buffer, (u32)xdg, 2);
            message_at = put_u32(buffer, message_at, 10);
            message_at = put_u32(buffer, message_at, 5);
            send_plain(socket, buffer, message_at);

            message_at = put_header(buffer, 10, 1);
            message_at = put_u32(buffer, message_at, 11);
            send_plain(socket, buffer, message_at);

            message_at = put_header(buffer, (u32)seat, 0);
            message_at = put_u32(buffer, message_at, 13);
            send_plain(socket, buffer, message_at);

            message_at = put_header(buffer, (u32)seat, 1);
            message_at = put_u32(buffer, message_at, 14);
            send_plain(socket, buffer, message_at);
            setup_sent = 1;
        }
        if (configured && !frame_sent && !done) {
            u32 message_at = put_header(buffer, (u32)shm, 0);
            message_at = put_u32(buffer, message_at, 7);
            message_at = put_u32(buffer, message_at, POOL_BYTES);
            send_with_fd(socket, buffer, message_at, (int)pool_fd);

            message_at = put_header(buffer, 7, 0);
            message_at = put_u32(buffer, message_at, 8);
            message_at = put_u32(buffer, message_at, 0);
            message_at = put_u32(buffer, message_at, WIDTH);
            message_at = put_u32(buffer, message_at, HEIGHT);
            message_at = put_u32(buffer, message_at, STRIDE);
            message_at = put_u32(buffer, message_at, 0);
            send_plain(socket, buffer, message_at);

            message_at = put_header(buffer, 5, 1);
            message_at = put_u32(buffer, message_at, 8);
            message_at = put_u32(buffer, message_at, 0);
            message_at = put_u32(buffer, message_at, 0);
            send_plain(socket, buffer, message_at);

            message_at = put_header(buffer, 5, 2);
            message_at = put_u32(buffer, message_at, 0);
            message_at = put_u32(buffer, message_at, 0);
            message_at = put_u32(buffer, message_at, WIDTH);
            message_at = put_u32(buffer, message_at, HEIGHT);
            send_plain(socket, buffer, message_at);

            message_at = put_header(buffer, 5, 6);
            send_plain(socket, buffer, message_at);

            message_at = put_header(buffer, 1, 0);
            message_at = put_u32(buffer, message_at, 6);
            send_plain(socket, buffer, message_at);
            frame_sent = 1;
        }
    }

    u_puts("[wldemo] committed ");
    u_putn((long)WIDTH);
    u_puts("x");
    u_putn((long)HEIGHT);
    u_puts("\n");
    sys_close(socket);
    return done ? 0 : 1;
}
