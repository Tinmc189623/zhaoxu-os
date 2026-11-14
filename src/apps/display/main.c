/*
 * Zhaoxu OS - Ring3 KMS display service (M1).
 *
 * Owns the virtio-gpu 2D scanout path through the modern virtio-pci
 * transport.  The service allocates two memfd-backed framebuffers, attaches
 * their physical pages as virtio-gpu resources and flips the scanout between
 * them.  A later milestone replaces the built-in test pattern with the
 * Wayland client protocol.
 */
#include <io.h>
#include <types.h>
#include <userlib.h>

extern u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset);
extern void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                               u32 value);

#define PCI_CAP_VIRTIO 0x09
#define PCI_CAP_COMMON 1
#define PCI_CAP_NOTIFY 2
#define PCI_CAP_DEVICE 4

#define GPU_CMD_GET_DISPLAY_INFO 0x0100
#define GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define GPU_CMD_SET_SCANOUT 0x0103
#define GPU_CMD_RESOURCE_FLUSH 0x0104
#define GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define GPU_FORMAT_BGRA 1
#define GPU_MAX_SCANOUTS 16
#define REQ_BYTES (64u * 1024u)
#define REQ_PAGES (REQ_BYTES / 4096u)

struct gpu_hdr {
    u32 type;
    u32 flags;
    u64 fence_id;
    u32 ctx_id;
    u8 ring_idx;
    u8 padding[3];
} __attribute__((packed));

struct gpu_rect {
    u32 x, y, width, height;
} __attribute__((packed));

struct gpu_display_one {
    struct gpu_rect r;
    u32 enabled;
    u32 flags;
} __attribute__((packed));

struct gpu_display_info {
    struct gpu_hdr hdr;
    struct gpu_display_one pmodes[GPU_MAX_SCANOUTS];
} __attribute__((packed));

struct gpu_resource_2d {
    u32 resource_id;
    u32 format;
    u32 width;
    u32 height;
} __attribute__((packed));

struct gpu_attach {
    u32 resource_id;
    u32 entry_count;
} __attribute__((packed));

struct gpu_attach_entry {
    u64 address;
    u32 length;
    u32 padding;
} __attribute__((packed));

struct gpu_set_scanout {
    struct gpu_rect r;
    u32 scanout_id;
    u32 resource_id;
} __attribute__((packed));

struct gpu_transfer {
    struct gpu_rect r;
    u64 offset;
    u32 resource_id;
    u32 padding;
} __attribute__((packed));

struct gpu_flush {
    struct gpu_rect r;
    u32 resource_id;
    u32 padding;
} __attribute__((packed));

struct virtq_desc {
    u64 address;
    u32 length;
    u16 flags;
    u16 next;
} __attribute__((packed));

struct virtq_avail {
    u16 flags;
    u16 index;
    u16 ring[64];
} __attribute__((packed));

struct virtq_used_elem {
    u32 id;
    u32 length;
} __attribute__((packed));

struct virtq_used {
    u16 flags;
    u16 index;
    struct virtq_used_elem ring[64];
} __attribute__((packed));

static struct {
    u8 *common;
    u8 *notify;
    u32 notify_multiplier;
    u16 notify_off;
    u64 queue_desc_phys;
    u64 queue_avail_phys;
    u64 queue_used_phys;
    u64 request_phys;
    u64 response_phys;
    u16 queue_size;
    u16 last_used;
} g_gpu;

static u8 g_attach[REQ_BYTES];

static void memzero(void *pointer, u32 length)
{
    u8 *bytes = pointer;
    for (u32 i = 0; i < length; i++)
        bytes[i] = 0;
}

static void memcopy(void *destination, const void *source, u32 length)
{
    u8 *dst = destination;
    const u8 *src = source;
    for (u32 i = 0; i < length; i++)
        dst[i] = src[i];
}

static u64 align_page(u64 value)
{
    return (value + 4095u) & ~4095ULL;
}

static u8 common8(u16 offset)
{
    return *(volatile u8 *)(g_gpu.common + offset);
}

static u16 common16(u16 offset)
{
    return *(volatile u16 *)(g_gpu.common + offset);
}

static void common8_write(u16 offset, u8 value)
{
    *(volatile u8 *)(g_gpu.common + offset) = value;
}

static void common16_write(u16 offset, u16 value)
{
    *(volatile u16 *)(g_gpu.common + offset) = value;
}

static void common32_write(u16 offset, u32 value)
{
    *(volatile u32 *)(g_gpu.common + offset) = value;
}

static void common64_write(u16 offset, u64 value)
{
    *(volatile u64 *)(g_gpu.common + offset) = value;
}

static u8 config8(u32 bus, u32 slot, u32 offset)
{
    u32 base = offset & ~3u;
    u32 value = pci_config_read32(bus, slot, 0, base);
    return (u8)(value >> ((offset & 3u) * 8u));
}

static u32 config32(u32 bus, u32 slot, u32 offset)
{
    u32 value = 0;
    for (u32 i = 0; i < 4; i++)
        value |= (u32)config8(bus, slot, offset + i) << (i * 8u);
    return value;
}

static u64 pci_bar(u32 bus, u32 slot, u32 bar)
{
    u32 low = config32(bus, slot, 0x10 + bar * 4);
    u64 physical = low & ~0xFULL;
    if (low & 0x4) {
        u32 high = config32(bus, slot, 0x14 + bar * 4);
        physical |= (u64)high << 32;
    }
    return physical;
}

/* Map one virtio capability region and return its starting pointer. */
static u8 *map_capability(u32 bus, u32 slot, u8 bar, u32 offset, u32 length)
{
    u64 physical = pci_bar(bus, slot, bar);
    if (!physical)
        return 0;
    u64 size = align_page((u64)offset + length);
    long mapped = sys_map_phys((unsigned long)physical, (unsigned long)size);
    if (mapped < 0)
        return 0;
    return (u8 *)(uintptr_t)((u64)mapped + offset);
}

static int virtio_modern_init(u32 bus, u32 slot)
{
    u32 status = config32(bus, slot, 0x04);
    if (!(status & 0x100000u))
        return -1;
    u8 capability = config8(bus, slot, 0x34);
    u32 device_multiplier = 1;
    for (u32 guard = 0; guard < 48 && capability; guard++) {
        u8 id = config8(bus, slot, capability);
        u8 next = config8(bus, slot, capability + 1);
        u8 length = config8(bus, slot, capability + 2);
        u8 type = config8(bus, slot, capability + 3);
        if (id == PCI_CAP_VIRTIO && length >= 16) {
            u8 bar = config8(bus, slot, capability + 4);
            u32 offset = config32(bus, slot, capability + 8);
            u32 region = config32(bus, slot, capability + 12);
            if (type == PCI_CAP_COMMON) {
                g_gpu.common = map_capability(bus, slot, bar, offset, region);
            } else if (type == PCI_CAP_NOTIFY) {
                g_gpu.notify = map_capability(bus, slot, bar, offset, region);
                if (length >= 20)
                    device_multiplier =
                        config32(bus, slot, capability + 16);
            }
        }
        capability = next;
    }
    if (!g_gpu.common || !g_gpu.notify)
        return -1;
    g_gpu.notify_multiplier = device_multiplier ? device_multiplier : 1;
    u_puts("[display] common=");
    u_puthex((u64)(uintptr_t)g_gpu.common);
    u_puts(" notify=");
    u_puthex((u64)(uintptr_t)g_gpu.notify);
    u_puts(" mult=");
    u_putn((long)g_gpu.notify_multiplier);
    u_puts("\n");

    common8_write(0x14, 0);
    common8_write(0x14, 1);
    common8_write(0x14, 1 | 2);
    /* Driver feature bit 32 (VIRTIO_F_VERSION_1) lives in page 1, bit 0. */
    common32_write(0x08, 1);
    common32_write(0x0C, 1);
    common32_write(0x08, 0);
    common32_write(0x0C, 0);
    common8_write(0x14, 1 | 2 | 8);
    if (!(common8(0x14) & 8))
        return -1;

    common16_write(0x16, 0);
    g_gpu.queue_size = common16(0x18);
    if (!g_gpu.queue_size || g_gpu.queue_size > 64)
        return -1;

    g_gpu.queue_desc_phys = sys_dev_alloc_page();
    g_gpu.queue_avail_phys = sys_dev_alloc_page();
    g_gpu.queue_used_phys = sys_dev_alloc_page();
    g_gpu.response_phys = sys_dev_alloc_page();
    if (!g_gpu.queue_desc_phys || !g_gpu.queue_avail_phys ||
        !g_gpu.queue_used_phys || !g_gpu.response_phys)
        return -1;
    memzero((void *)(uintptr_t)g_gpu.queue_desc_phys, 4096);
    memzero((void *)(uintptr_t)g_gpu.queue_avail_phys, 4096);
    memzero((void *)(uintptr_t)g_gpu.queue_used_phys, 4096);
    memzero((void *)(uintptr_t)g_gpu.response_phys, 4096);

    u64 base = 0;
    u32 contiguous = 0;
    for (u32 attempt = 0; attempt < 64 && contiguous < REQ_PAGES; attempt++) {
        u64 page = sys_dev_alloc_page();
        if (!page)
            break;
        if (!contiguous) {
            base = page;
            contiguous = 1;
        } else if (page == base + (u64)contiguous * 4096) {
            contiguous++;
        } else {
            base = page;
            contiguous = 1;
        }
    }
    if (contiguous < REQ_PAGES)
        return -1;
    g_gpu.request_phys = base;
    memzero((void *)(uintptr_t)g_gpu.request_phys, REQ_BYTES);

    g_gpu.notify_off = common16(0x1E);
    common64_write(0x20, g_gpu.queue_desc_phys);
    common64_write(0x28, g_gpu.queue_avail_phys);
    common64_write(0x30, g_gpu.queue_used_phys);
    common16_write(0x1C, 1);
    common8_write(0x14, 1 | 2 | 8 | 4);
    g_gpu.last_used = ((struct virtq_used *)(uintptr_t)g_gpu.queue_used_phys)
                          ->index;
    u_puts("[display] queue=");
    u_putn((long)g_gpu.queue_size);
    u_puts(" notify_off=");
    u_putn((long)g_gpu.notify_off);
    u_puts("\n");
    return 0;
}

static int gpu_init(void)
{
    for (u32 bus = 0; bus < 256; bus++) {
        for (u32 slot = 0; slot < 32; slot++) {
            u32 id = pci_config_read32(bus, slot, 0, 0);
            if (id == 0xFFFFFFFFu || id == 0)
                continue;
            if ((id & 0xFFFF) != 0x1AF4)
                continue;
            if ((id >> 16) != 0x1050)
                continue;
            u32 command = pci_config_read32(bus, slot, 0, 4);
            pci_config_write32(bus, slot, 0, 4, command | 0x2);
            return virtio_modern_init(bus, slot);
        }
    }
    return -1;
}

static int gpu_command(u32 type, const void *payload, u32 payload_size,
                       void *response, u32 response_size)
{
    if (!g_gpu.common || payload_size > REQ_BYTES - sizeof(struct gpu_hdr))
        return -1;
    if (response_size < sizeof(struct gpu_hdr))
        response_size = (u32)sizeof(struct gpu_hdr);

    u8 *request = (u8 *)(uintptr_t)g_gpu.request_phys;
    memzero(request, sizeof(struct gpu_hdr) + payload_size);
    struct gpu_hdr *header = (struct gpu_hdr *)request;
    header->type = type;
    if (payload && payload_size)
        memcopy(request + sizeof(*header), payload, payload_size);
    memzero((void *)(uintptr_t)g_gpu.response_phys, response_size);

    struct virtq_desc *desc =
        (struct virtq_desc *)(uintptr_t)g_gpu.queue_desc_phys;
    desc[0].address = g_gpu.request_phys;
    desc[0].length = (u32)sizeof(*header) + payload_size;
    desc[0].flags = 1; /* VIRTQ_DESC_F_NEXT */
    desc[0].next = 1;
    desc[1].address = g_gpu.response_phys;
    desc[1].length = response_size;
    desc[1].flags = 2;
    desc[1].next = 0;

    struct virtq_avail *avail =
        (struct virtq_avail *)(uintptr_t)g_gpu.queue_avail_phys;
    avail->ring[avail->index % g_gpu.queue_size] = 0;
    __sync_synchronize();
    avail->index = (u16)(avail->index + 1);
    __sync_synchronize();
    *(volatile u16 *)(g_gpu.notify +
                      (u64)g_gpu.notify_off * g_gpu.notify_multiplier) = 0;

    struct virtq_used *used =
        (struct virtq_used *)(uintptr_t)g_gpu.queue_used_phys;
    for (u32 spin = 0; spin < 10000000u; spin++) {
        __sync_synchronize();
        if (used->index != g_gpu.last_used)
            break;
    }
    if (used->index == g_gpu.last_used) {
        u_puts("[display] control command timeout\n");
        return -1;
    }
    g_gpu.last_used = used->index;
    if (response && response_size)
        memcopy(response, (void *)(uintptr_t)g_gpu.response_phys,
                response_size);
    return 0;
}

static int display_info(u32 *width, u32 *height)
{
    struct gpu_display_info info;
    memzero(&info, sizeof(info));
    if (gpu_command(GPU_CMD_GET_DISPLAY_INFO, NULL, 0, &info,
                    sizeof(info)) != 0) {
        u_puts("[display] display info command failed\n");
        return -1;
    }
    u_puts("[display] display resp type=0x");
    u_puthex(info.hdr.type);
    u_puts(" first=");
    u_putn((long)info.pmodes[0].r.width);
    u_puts("x");
    u_putn((long)info.pmodes[0].r.height);
    u_puts(" enabled=");
    u_putn((long)info.pmodes[0].enabled);
    u_puts("\n");
    for (u32 i = 0; i < GPU_MAX_SCANOUTS; i++) {
        if (info.pmodes[i].enabled && info.pmodes[i].r.width &&
            info.pmodes[i].r.height) {
            *width = info.pmodes[i].r.width;
            *height = info.pmodes[i].r.height;
            return 0;
        }
    }
    return -1;
}

static int create_resource(u32 id, u32 width, u32 height)
{
    struct gpu_resource_2d request;
    request.resource_id = id;
    request.format = GPU_FORMAT_BGRA;
    request.width = width;
    request.height = height;
    return gpu_command(GPU_CMD_RESOURCE_CREATE_2D, &request, sizeof(request),
                       NULL, 0);
}

static int attach_backing(u32 id, long fd, u32 pages)
{
    struct gpu_attach *head = (struct gpu_attach *)g_attach;
    head->resource_id = id;
    head->entry_count = pages;
    struct gpu_attach_entry *entries =
        (struct gpu_attach_entry *)(g_attach + sizeof(*head));
    for (u32 i = 0; i < pages; i++) {
        long physical = sys_memfd_phys(fd, i);
        if (physical <= 0)
            return -1;
        entries[i].address = (u64)physical;
        entries[i].length = 4096;
        entries[i].padding = 0;
    }
    u32 bytes = (u32)sizeof(*head) + pages * (u32)sizeof(*entries);
    return gpu_command(GPU_CMD_RESOURCE_ATTACH_BACKING, g_attach, bytes, NULL,
                       0);
}

static int transfer_2d(u32 id, u32 width, u32 height)
{
    struct gpu_transfer transfer;
    transfer.r.x = 0;
    transfer.r.y = 0;
    transfer.r.width = width;
    transfer.r.height = height;
    transfer.offset = 0;
    transfer.resource_id = id;
    transfer.padding = 0;
    return gpu_command(GPU_CMD_TRANSFER_TO_HOST_2D, &transfer,
                       sizeof(transfer), NULL, 0);
}

static int flush_2d(u32 id, u32 width, u32 height)
{
    struct gpu_flush flush;
    flush.r.x = 0;
    flush.r.y = 0;
    flush.r.width = width;
    flush.r.height = height;
    flush.resource_id = id;
    flush.padding = 0;
    return gpu_command(GPU_CMD_RESOURCE_FLUSH, &flush, sizeof(flush), NULL, 0);
}

static int set_scanout(u32 id, u32 width, u32 height)
{
    struct gpu_set_scanout scanout;
    scanout.r.x = 0;
    scanout.r.y = 0;
    scanout.r.width = width;
    scanout.r.height = height;
    scanout.scanout_id = 0;
    scanout.resource_id = id;
    return gpu_command(GPU_CMD_SET_SCANOUT, &scanout, sizeof(scanout), NULL, 0);
}

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

/* Hand the compositor the two scanout memfds and the current mode. */
static int send_mode(int client, u32 width, u32 height, u32 stride, u32 bytes,
                     long fds[2])
{
    struct display_mode mode;
    mode.magic = DISPLAY_MODE_MAGIC;
    mode.width = width;
    mode.height = height;
    mode.stride = stride;
    mode.bytes = bytes;

    struct iovec iov;
    iov.iov_base = &mode;
    iov.iov_len = sizeof(mode);
    u8 control[CMSG_SPACE(2 * sizeof(int))];
    memzero(control, sizeof(control));
    struct msghdr message;
    memzero(&message, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    struct cmsghdr *cmsg = (struct cmsghdr *)control;
    cmsg->cmsg_len = CMSG_LEN(2 * sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    int *passed = (int *)CMSG_DATA(cmsg);
    passed[0] = (int)fds[0];
    passed[1] = (int)fds[1];
    return sys_sendmsg(client, &message, 0) == (long)sizeof(mode) ? 0 : -1;
}

static int display_listen(void)
{
    int fd = (int)sys_socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un address;
    memzero(&address, sizeof(address));
    address.sun_family = AF_UNIX;
    u32 length = 0;
    const char *path = "/run/display";
    while (path[length]) {
        address.sun_path[length] = path[length];
        length++;
    }
    address.sun_path[length++] = 0;
    if (sys_bind(fd, &address, 2 + length) != 0 ||
        sys_listen(fd, 1) != 0) {
        sys_close(fd);
        return -1;
    }
    if (sys_fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        sys_close(fd);
        return -1;
    }
    return fd;
}

static void draw_pattern(u32 *pixels, u32 width, u32 height, u32 frame)
{
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u32 r = (x * 255) / width;
            u32 g = (y * 255) / height;
            u32 b = 32 + ((frame * 37) % 192);
            u32 color = 0xFF000000u | (r << 16) | (g << 8) | b;
            u32 bar_x = (frame * 23) % (width > 320 ? width - 320 : 1);
            u32 bar_y = height / 2 - 80;
            if (x >= bar_x && x < bar_x + 320 && y >= bar_y &&
                y < bar_y + 160)
                color = 0xFF1E6FB8u;
            pixels[(u64)y * width + x] = color;
        }
    }
}

int user_main(long *stack)
{
    (void)stack;

    u32 width = 0;
    u32 height = 0;

    if (gpu_init() != 0) {
        u_puts("[display] no modern virtio-gpu; KMS service idle\n");
        return 0;
    }
    if (display_info(&width, &height) != 0) {
        u_puts("[display] virtio-gpu display info failed\n");
        return 1;
    }
    u_puts("[display] virtio-gpu ");
    u_putn((long)width);
    u_puts("x");
    u_putn((long)height);
    u_puts(" KMS online\n");

    u64 bytes = (u64)width * height * 4u;
    if (bytes > 64u * 1024u * 1024u) {
        u_puts("[display] mode too large for memfd\n");
        return 1;
    }
    u32 pages = (u32)((bytes + 4095) / 4096);

    long fds[2];
    u32 *buffers[2];
    for (u32 i = 0; i < 2; i++) {
        fds[i] = sys_memfd_create("scanout", 0);
        if (fds[i] < 0 || sys_ftruncate(fds[i], bytes) != 0) {
            u_puts("[display] memfd allocation failed\n");
            return 1;
        }
        struct zos_mmap_args args;
        args.address = 0;
        args.length = bytes;
        args.protection = 3;
        args.flags = 1;
        args.fd = fds[i];
        args.offset = 0;
        long address = sys_mmap(&args);
        if (address < 0) {
            u_puts("[display] memfd mmap failed\n");
            return 1;
        }
        buffers[i] = (u32 *)address;
        if (create_resource((u32)i + 1, width, height) != 0 ||
            attach_backing((u32)i + 1, fds[i], pages) != 0) {
            u_puts("[display] resource setup failed\n");
            return 1;
        }
    }

    int listen_fd = display_listen();
    if (listen_fd < 0) {
        u_puts("[display] /run/display listen failed\n");
        return 1;
    }

    int client = -1;
    u32 frame = 0;
    u32 active = 0;
    for (;;) {
        if (client < 0) {
            client = (int)sys_accept(listen_fd, 0, 0);
            if (client >= 0) {
                if (send_mode(client, width, height, width * 4,
                              (u32)bytes, fds) != 0) {
                    sys_close(client);
                    client = -1;
                } else {
                    u_puts("[display] compositor connected\n");
                }
            } else {
                draw_pattern(buffers[active], width, height, frame);
                transfer_2d(active + 1, width, height);
                flush_2d(active + 1, width, height);
                set_scanout(active + 1, width, height);
                frame++;
                active ^= 1;
                sys_sleep(500);
                continue;
            }
        }

        struct display_commit commit;
        long length = sys_read(client, &commit, sizeof(commit));
        if (length == (long)sizeof(commit) &&
            commit.magic == DISPLAY_COMMIT_MAGIC && commit.index < 2) {
            transfer_2d(commit.index + 1, width, height);
            flush_2d(commit.index + 1, width, height);
            set_scanout(commit.index + 1, width, height);
        } else if (length <= 0) {
            sys_close(client);
            client = -1;
            u_puts("[display] compositor disconnected\n");
        }
    }
}
