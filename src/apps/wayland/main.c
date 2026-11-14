/*
 * Zhaoxu OS - minimal Wayland core server (M2).
 *
 * Speaks the Wayland wire format over the M0 AF_UNIX transport and passes
 * wl_shm pool descriptors with SCM_RIGHTS.  Supported interfaces:
 * wl_display, wl_registry, wl_callback, wl_compositor, wl_surface,
 * wl_shm, wl_shm_pool and wl_buffer.  The committed buffer is validated and
 * logged; KMS composition is the next milestone.
 */
#include <types.h>
#include <userlib.h>

#define SOCK_PATH "/run/wayland-0"
#define MAX_OBJECTS 64
#define MAX_MESSAGE 4096

enum object_type {
    OBJ_DISPLAY = 1,
    OBJ_REGISTRY,
    OBJ_CALLBACK,
    OBJ_COMPOSITOR,
    OBJ_SURFACE,
    OBJ_SHM,
    OBJ_POOL,
    OBJ_BUFFER,
    OBJ_XDG_WM_BASE,
    OBJ_XDG_SURFACE,
    OBJ_XDG_TOPLEVEL,
    OBJ_SEAT,
    OBJ_POINTER,
    OBJ_KEYBOARD
};

struct object {
    int used;
    u32 id;
    int type;
    int fd;
    u32 size;
    u32 offset;
    u32 width;
    u32 height;
    u32 stride;
    u32 format;
    u32 pool_id;
    u32 buffer_id;
};

static struct object g_objects[MAX_OBJECTS];
static int g_listen = -1;
static int g_client = -1;
static int g_pending_fd = -1;

struct display_mode {
    u32 magic;
    u32 width;
    u32 height;
    u32 stride;
    u32 bytes;
};

struct display_commit {
    u32 magic;
    u32 index;
};

#define DISPLAY_MODE_MAGIC 0x5A444D44u
#define DISPLAY_COMMIT_MAGIC 0x5A434D54u

static int g_display_fd = -1;
static u32 g_out_width;
static u32 g_out_height;
static u32 g_out_stride;
static u32 *g_out[2];
static u32 g_out_active;

static u32 str_len(const char *text);
static int str_equal(const char *a, const char *b);
static void copy_bytes(void *destination, const void *source, u32 length);
static void zero_bytes(void *destination, u32 length);

static int connect_display(void)
{
    for (int attempt = 0; attempt < 100; attempt++) {
        int fd = (int)sys_socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        struct sockaddr_un address;
        zero_bytes(&address, sizeof(address));
        address.sun_family = AF_UNIX;
        u32 length = str_len("/run/display") + 1;
        copy_bytes(address.sun_path, "/run/display", length);
        if (sys_connect(fd, &address, 2 + length) == 0)
            return fd;
        sys_close(fd);
        sys_sleep(10);
    }
    return -1;
}

static int display_setup(void)
{
    g_display_fd = connect_display();
    if (g_display_fd < 0)
        return -1;

    struct display_mode mode;
    struct iovec iov;
    iov.iov_base = &mode;
    iov.iov_len = sizeof(mode);
    u8 control[CMSG_SPACE(2 * sizeof(int))];
    zero_bytes(control, sizeof(control));
    struct msghdr message;
    zero_bytes(&message, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    long length = sys_recvmsg(g_display_fd, &message, 0);
    if (length != (long)sizeof(mode) || mode.magic != DISPLAY_MODE_MAGIC ||
        message.msg_controllen < CMSG_LEN(2 * sizeof(int)))
        return -1;
    struct cmsghdr *cmsg = (struct cmsghdr *)control;
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
        return -1;
    int *fds = (int *)CMSG_DATA(cmsg);
    for (u32 i = 0; i < 2; i++) {
        struct zos_mmap_args args;
        args.address = 0;
        args.length = mode.bytes;
        args.protection = 3;
        args.flags = 1;
        args.fd = fds[i];
        args.offset = 0;
        long mapped = sys_mmap(&args);
        if (mapped < 0)
            return -1;
        g_out[i] = (u32 *)(uintptr_t)mapped;
    }
    g_out_width = mode.width;
    g_out_height = mode.height;
    g_out_stride = mode.stride;
    u_puts("[wayland] display mode ");
    u_putn((long)g_out_width);
    u_puts("x");
    u_putn((long)g_out_height);
    u_puts("\n");
    return 0;
}

static void composite(const struct object *buffer, const u32 *source)
{
    if (g_display_fd < 0 || !g_out[0] || !g_out[1] || !buffer)
        return;
    u32 index = g_out_active ^ 1u;
    u32 *target = g_out[index];
    u32 out_stride = g_out_stride / 4u;
    for (u32 y = 0; y < g_out_height; y++)
        for (u32 x = 0; x < g_out_width; x++)
            target[(u64)y * out_stride + x] = 0xFF16283Au;

    u32 copy_w = buffer->width;
    u32 copy_h = buffer->height;
    if (copy_w > g_out_width)
        copy_w = g_out_width;
    if (copy_h > g_out_height)
        copy_h = g_out_height;
    u32 left = (g_out_width - copy_w) / 2;
    u32 top = (g_out_height - copy_h) / 2;
    u32 source_stride = buffer->stride ? buffer->stride / 4u : copy_w;
    const u32 *pixels = source + buffer->offset / 4u;
    for (u32 y = 0; y < copy_h; y++)
        for (u32 x = 0; x < copy_w; x++)
            target[(u64)(top + y) * out_stride + left + x] =
                pixels[(u64)y * source_stride + x];

    struct display_commit commit;
    commit.magic = DISPLAY_COMMIT_MAGIC;
    commit.index = index;
    if (sys_write(g_display_fd, &commit, sizeof(commit)) ==
        (long)sizeof(commit))
        g_out_active = index;
}

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

static struct object *object_find(u32 id)
{
    for (int i = 0; i < MAX_OBJECTS; i++)
        if (g_objects[i].used && g_objects[i].id == id)
            return &g_objects[i];
    return 0;
}

static struct object *object_new(u32 id, int type)
{
    for (int i = 0; i < MAX_OBJECTS; i++) {
        struct object *object = &g_objects[i];
        if (object->used)
            continue;
        zero_bytes(object, sizeof(*object));
        object->used = 1;
        object->id = id;
        object->type = type;
        object->fd = -1;
        return object;
    }
    return 0;
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

static void finish_message(u8 *buffer, u32 size)
{
    *(u16 *)(buffer + 6) = (u16)size;
}

static int send_all(const void *data, u32 size)
{
    if (g_client < 0)
        return -1;
    return sys_write(g_client, data, size) == (long)size ? 0 : -1;
}

static void send_global(u32 registry, u32 name, const char *interface,
                        u32 version)
{
    u8 message[128];
    u32 at = put_header(message, registry, 0);
    at = put_u32(message, at, name);
    at = put_string(message, at, interface);
    at = put_u32(message, at, version);
    finish_message(message, at);
    send_all(message, at);
}

static void send_done(u32 callback, u32 data)
{
    u8 message[16];
    u32 at = put_header(message, callback, 0);
    at = put_u32(message, at, data);
    finish_message(message, at);
    send_all(message, at);
}

static u32 g_serial = 1;

static void send_xdg_configure(u32 toplevel, u32 surface)
{
    u8 message[32];
    u32 at = put_header(message, toplevel, 0);
    at = put_u32(message, at, 400);
    at = put_u32(message, at, 300);
    at = put_u32(message, at, 0); /* empty states array */
    finish_message(message, at);
    send_all(message, at);

    at = put_header(message, surface, 0);
    at = put_u32(message, at, g_serial++);
    finish_message(message, at);
    send_all(message, at);
}

static void send_seat_caps(u32 seat)
{
    u8 message[16];
    u32 at = put_header(message, seat, 0);
    at = put_u32(message, at, 3); /* pointer | keyboard */
    finish_message(message, at);
    send_all(message, at);
}

static void send_pointer_events(u32 pointer, u32 surface)
{
    u8 message[32];
    u32 time = (u32)sys_uptime();
    u32 at = put_header(message, pointer, 0); /* enter */
    at = put_u32(message, at, g_serial++);
    at = put_u32(message, at, surface);
    at = put_u32(message, at, 160u << 8);
    at = put_u32(message, at, 120u << 8);
    finish_message(message, at);
    send_all(message, at);

    at = put_header(message, pointer, 2); /* motion */
    at = put_u32(message, at, time);
    at = put_u32(message, at, 160u << 8);
    at = put_u32(message, at, 120u << 8);
    finish_message(message, at);
    send_all(message, at);

    for (u32 state = 1; state <= 2; state++) {
        at = put_header(message, pointer, 3); /* button */
        at = put_u32(message, at, g_serial++);
        at = put_u32(message, at, time);
        at = put_u32(message, at, 0x110);
        at = put_u32(message, at, state == 1 ? 1 : 0);
        finish_message(message, at);
        send_all(message, at);
    }
}

static u32 get_u32(const u8 *message, u32 at)
{
    return *(const u32 *)(message + at);
}

static int get_string(const u8 *message, u32 size, u32 at, char *out,
                      u32 capacity)
{
    if (at + 4 > size)
        return -1;
    u32 length = get_u32(message, at);
    at += 4;
    if (!length || length > capacity || at + length > size)
        return -1;
    copy_bytes(out, message + at, length);
    out[length - 1] = 0;
    return 0;
}

static int dispatch(u32 id, u16 opcode, const u8 *message, u32 size)
{
    struct object *object = object_find(id);
    if (!object)
        return -1;

    switch (object->type) {
    case OBJ_DISPLAY:
        if (opcode == 0 && size >= 12) {
            struct object *callback =
                object_new(get_u32(message, 8), OBJ_CALLBACK);
            if (callback)
                send_done(callback->id, 0);
        } else if (opcode == 1 && size >= 12) {
            u32 registry_id = get_u32(message, 8);
            if (object_new(registry_id, OBJ_REGISTRY)) {
                send_global(registry_id, 1, "wl_compositor", 1);
                send_global(registry_id, 2, "wl_shm", 1);
                send_global(registry_id, 3, "xdg_wm_base", 1);
                send_global(registry_id, 4, "wl_seat", 1);
            }
        }
        break;
    case OBJ_REGISTRY:
        if (opcode == 0 && size >= 20) {
            u32 new_id = 0;
            char interface[64];
            u32 at = 8;
            at += 4;
            if (get_string(message, size, at, interface, sizeof(interface)))
                break;
            at += 4 + ((str_len(interface) + 1 + 3u) & ~3u);
            at += 4;
            if (at + 4 > size)
                break;
            new_id = get_u32(message, at);
            if (str_equal(interface, "wl_compositor"))
                object_new(new_id, OBJ_COMPOSITOR);
            else if (str_equal(interface, "wl_shm"))
                object_new(new_id, OBJ_SHM);
            else if (str_equal(interface, "xdg_wm_base"))
                object_new(new_id, OBJ_XDG_WM_BASE);
            else if (str_equal(interface, "wl_seat")) {
                object_new(new_id, OBJ_SEAT);
                send_seat_caps(new_id);
            }
        }
        break;
    case OBJ_COMPOSITOR:
        if (opcode == 0 && size >= 12)
            object_new(get_u32(message, 8), OBJ_SURFACE);
        break;
    case OBJ_SHM:
        if (opcode == 0 && size >= 16) {
            u32 pool_id = get_u32(message, 8);
            u32 pool_size = get_u32(message, 12);
            struct object *pool = object_new(pool_id, OBJ_POOL);
            if (pool) {
                pool->fd = g_pending_fd;
                pool->size = pool_size;
                g_pending_fd = -1;
            }
        }
        break;
    case OBJ_POOL:
        if (opcode == 0 && size >= 32) {
            u32 buffer_id = get_u32(message, 8);
            struct object *buffer = object_new(buffer_id, OBJ_BUFFER);
            if (buffer) {
                buffer->pool_id = object->id;
                buffer->offset = get_u32(message, 12);
                buffer->width = get_u32(message, 16);
                buffer->height = get_u32(message, 20);
                buffer->stride = get_u32(message, 24);
                buffer->format = get_u32(message, 28);
            }
        }
        break;
    case OBJ_XDG_WM_BASE:
        if (opcode == 2 && size >= 16) {
            u32 new_id = get_u32(message, 8);
            u32 surface = get_u32(message, 12);
            struct object *xdg = object_new(new_id, OBJ_XDG_SURFACE);
            if (xdg)
                xdg->buffer_id = surface;
        }
        break;
    case OBJ_XDG_SURFACE:
        if (opcode == 1 && size >= 12) {
            u32 new_id = get_u32(message, 8);
            if (object_new(new_id, OBJ_XDG_TOPLEVEL))
                send_xdg_configure(new_id, object->id);
        } else if (opcode == 4 && size >= 12) {
            u_puts("[wayland] xdg ack_configure ");
            u_putn((long)get_u32(message, 8));
            u_puts("\n");
        }
        break;
    case OBJ_SEAT:
        if (opcode == 0 && size >= 12) {
            u32 new_id = get_u32(message, 8);
            if (object_new(new_id, OBJ_POINTER)) {
                u32 surface = 0;
                for (int i = 0; i < MAX_OBJECTS; i++)
                    if (g_objects[i].used &&
                        g_objects[i].type == OBJ_SURFACE) {
                        surface = g_objects[i].id;
                        break;
                    }
                send_pointer_events(new_id, surface);
            }
        } else if (opcode == 1 && size >= 12) {
            object_new(get_u32(message, 8), OBJ_KEYBOARD);
        }
        break;
    case OBJ_SURFACE:
        if (opcode == 1 && size >= 12) {
            object->buffer_id = get_u32(message, 8);
        } else if (opcode == 6) {
            struct object *buffer = object_find(object->buffer_id);
            if (!buffer)
                break;
            struct object *pool = object_find(buffer->pool_id);
            if (!pool || pool->fd < 0)
                break;
            struct zos_mmap_args args;
            args.address = 0;
            args.length = pool->size;
            args.protection = 1;
            args.flags = 1;
            args.fd = pool->fd;
            args.offset = 0;
            long mapped = sys_mmap(&args);
            if (mapped < 0)
                break;
            const u32 *pixels = (const u32 *)(uintptr_t)mapped;
            u32 pixel = pixels[buffer->offset / 4];
            u_puts("[wayland] commit ");
            u_putn((long)buffer->width);
            u_puts("x");
            u_putn((long)buffer->height);
            u_puts(" stride=");
            u_putn((long)buffer->stride);
            u_puts(" first=0x");
            u_puthex(pixel);
            u_puts("\n");
            composite(buffer, pixels);
        }
        break;
    default:
        break;
    }
    return 0;
}

static void serve_client(void)
{
    u8 buffer[MAX_MESSAGE];
    u32 have = 0;
    for (;;) {
        struct iovec iov;
        struct msghdr message;
        u8 control[256];
        iov.iov_base = buffer + have;
        iov.iov_len = sizeof(buffer) - have;
        zero_bytes(&message, sizeof(message));
        message.msg_iov = &iov;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        long length = sys_recvmsg(g_client, &message, 0);
        if (length <= 0)
            return;
        if (message.msg_controllen >= sizeof(struct cmsghdr)) {
            struct cmsghdr *cmsg = (struct cmsghdr *)control;
            if (cmsg->cmsg_level == SOL_SOCKET &&
                cmsg->cmsg_type == SCM_RIGHTS)
                g_pending_fd = *(int *)CMSG_DATA(cmsg);
        }
        have += (u32)length;

        u32 at = 0;
        while (have - at >= 8) {
            u32 id = get_u32(buffer + at, 0);
            u16 opcode = *(u16 *)(buffer + at + 4);
            u16 size = *(u16 *)(buffer + at + 6);
            if (size < 8 || size > have - at)
                break;
            dispatch(id, opcode, buffer + at, size);
            at += size;
        }
        if (at) {
            for (u32 i = at; i < have; i++)
                buffer[i - at] = buffer[i];
            have -= at;
        }
    }
}

int user_main(long *stack)
{
    (void)stack;

    for (int i = 0; i < MAX_OBJECTS; i++)
        g_objects[i].used = 0;

    g_listen = (int)sys_socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen < 0) {
        u_puts("[wayland] socket failed\n");
        return 1;
    }
    struct sockaddr_un address;
    zero_bytes(&address, sizeof(address));
    address.sun_family = AF_UNIX;
    u32 length = str_len(SOCK_PATH) + 1;
    copy_bytes(address.sun_path, SOCK_PATH, length);
    if (sys_bind(g_listen, &address, 2 + length) != 0 ||
        sys_listen(g_listen, 4) != 0) {
        u_puts("[wayland] bind/listen failed\n");
        return 1;
    }
    if (display_setup() != 0) {
        u_puts("[wayland] /bin/display unavailable; commit-only mode\n");
        g_display_fd = -1;
    }
    u_puts("[wayland] listening on /run/wayland-0\n");

    object_new(1, OBJ_DISPLAY);
    for (;;) {
        int client = (int)sys_accept(g_listen, 0, 0);
        if (client < 0) {
            sys_sleep(10);
            continue;
        }
        g_client = client;
        g_pending_fd = -1;
        u_puts("[wayland] client connected\n");
        serve_client();
        if (g_pending_fd >= 0)
            sys_close(g_pending_fd);
        sys_close(client);
        g_client = -1;
        u_puts("[wayland] client disconnected\n");
    }
}
