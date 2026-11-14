/*
 * Zhaoxu OS - virtio-gpu 3D acceleration driver.
 *
 * Implements the legacy virtio-pci transport for the control queue and the
 * 3D subset of the virtio-gpu device: capability discovery, virgl context
 * creation, 3D resource creation and SUBMIT_3D command submission.  A
 * userspace GL driver supplies virgl command buffers through the exported
 * gpu3d_submit() entry point; the transport, fencing and resource lifetime
 * stay here.
 */
#include <dev.h>
#include <io.h>
#include <types.h>

extern u32 pci_config_read32(u32 bus, u32 slot, u32 function, u32 offset);
extern void pci_config_write32(u32 bus, u32 slot, u32 function, u32 offset,
                               u32 value);
extern u64 pmm_alloc_page(void);
extern void buffer_zero(void *destination, u32 bytes);
extern void console_write(const char *text);

#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_SIZE 0x0C
#define VIRTIO_PCI_QUEUE_SELECT 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_STATUS 0x12

#define VIRTIO_STATUS_ACK 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_GPU_F_VIRGL (1u << 0)

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO 0x0101
#define VIRTIO_GPU_CMD_CTX_CREATE 0x0200
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D 0x0202
#define VIRTIO_GPU_CMD_SUBMIT_3D 0x0204

#define VIRTIO_GPU_FLAG_FENCE (1u << 0)
#define VIRTIO_GPU_MAX_SCANOUTS 16

struct virtio_gpu_ctrl_hdr {
    u32 type;
    u32 flags;
    u64 fence_id;
    u32 ctx_id;
    u8 ring_idx;
    u8 padding[3];
} __attribute__((packed));

struct virtio_gpu_rect {
    u32 x, y, width, height;
} __attribute__((packed));

struct virtio_gpu_display_one {
    struct virtio_gpu_rect r;
    u32 enabled;
    u32 flags;
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed));

struct virtio_gpu_resp_capset_info {
    struct virtio_gpu_ctrl_hdr hdr;
    u32 capset_id;
    u32 capset_max_version;
    u32 capset_max_size;
    u32 padding;
} __attribute__((packed));

struct virtio_gpu_ctx_create {
    struct virtio_gpu_ctrl_hdr hdr;
    u32 nlen;
    u32 context_init;
    char debug_name[64];
} __attribute__((packed));

struct virtio_gpu_resource_create_3d {
    struct virtio_gpu_ctrl_hdr hdr;
    u32 resource_id;
    u32 format;
    u32 bind;
    u32 width;
    u32 height;
    u32 depth;
    u32 array_size;
    u32 last_level;
    u32 nr_samples;
    u32 flags;
} __attribute__((packed));

struct virtio_gpu_submit_3d {
    struct virtio_gpu_ctrl_hdr hdr;
    u32 size;
} __attribute__((packed));

struct virtio_desc {
    u64 address;
    u32 length;
    u16 flags;
    u16 next;
} __attribute__((packed));

struct virtio_avail {
    u16 flags;
    u16 index;
    u16 ring[64];
} __attribute__((packed));

struct virtio_used_elem {
    u32 id;
    u32 length;
} __attribute__((packed));

struct virtio_used {
    u16 flags;
    u16 index;
    struct virtio_used_elem ring[64];
} __attribute__((packed));

struct virtio_gpu {
    u16 io_base;
    u64 queue_phys;
    u64 request_phys;
    u64 response_phys;
    u16 queue_size;
    u16 avail_index;
    u16 last_used;
    u32 features;
    u32 capset_id;
    u32 ctx_id;
    int ready;
};

static struct virtio_gpu g_gpu;

static u32 in32(u16 offset) { return inl(g_gpu.io_base + offset); }
static u16 in16(u16 offset) { return inw(g_gpu.io_base + offset); }
static void out32(u16 offset, u32 value)
{
    outl(g_gpu.io_base + offset, value);
}
static void out16(u16 offset, u16 value)
{
    outw(g_gpu.io_base + offset, value);
}
static void out8(u16 offset, u8 value)
{
    outb(g_gpu.io_base + offset, value);
}

static struct virtio_desc *descriptors(void)
{
    return (struct virtio_desc *)(uintptr_t)g_gpu.queue_phys;
}

static struct virtio_avail *avail_ring(void)
{
    return (struct virtio_avail *)(uintptr_t)(g_gpu.queue_phys + 1024);
}

static struct virtio_used *used_ring(void)
{
    return (struct virtio_used *)(uintptr_t)(g_gpu.queue_phys + 2048);
}

/* Submit a request/response pair through the control queue. */
static int gpu_command(u32 type, const void *payload, u32 payload_size,
                       void *response, u32 response_size, u32 fence)
{
    if (!g_gpu.ready)
        return -1;
    u8 *request = (u8 *)(uintptr_t)g_gpu.request_phys;
    struct virtio_gpu_ctrl_hdr *header =
        (struct virtio_gpu_ctrl_hdr *)request;
    buffer_zero(request, 4096);
    header->type = type;
    header->flags = fence ? VIRTIO_GPU_FLAG_FENCE : 0;
    header->fence_id = fence;
    header->ctx_id = g_gpu.ctx_id;
    if (payload && payload_size) {
        u8 *destination = request + sizeof(*header);
        u32 copy = payload_size;
        if (copy > 4096 - sizeof(*header))
            copy = 4096 - sizeof(*header);
        for (u32 i = 0; i < copy; i++)
            destination[i] = ((const u8 *)payload)[i];
    }
    buffer_zero((void *)(uintptr_t)g_gpu.response_phys, 4096);

    struct virtio_desc *desc = descriptors();
    u32 index = g_gpu.avail_index % g_gpu.queue_size;
    desc[index].address = g_gpu.request_phys;
    desc[index].length = (u32)sizeof(*header) + payload_size;
    desc[index].flags = 0; /* device-readable */
    desc[index].next = (u16)((index + 1) % g_gpu.queue_size);
    u32 response_index = (index + 1) % g_gpu.queue_size;
    desc[response_index].address = g_gpu.response_phys;
    desc[response_index].length = response_size;
    desc[response_index].flags = 2; /* device-writable */
    desc[response_index].next = 0;

    struct virtio_avail *avail = avail_ring();
    avail->ring[avail->index % g_gpu.queue_size] = (u16)index;
    __sync_synchronize();
    avail->index = (u16)(avail->index + 1);
    g_gpu.avail_index = avail->index;
    out16(VIRTIO_PCI_QUEUE_NOTIFY, 0);

    struct virtio_used *used = used_ring();
    for (u32 spin = 0; spin < 10000000u; spin++) {
        __sync_synchronize();
        if (used->index != g_gpu.last_used)
            break;
    }
    if (used->index == g_gpu.last_used)
        return -1;
    g_gpu.last_used = used->index;
    if (response && response_size) {
        u8 *source = (u8 *)(uintptr_t)g_gpu.response_phys;
        for (u32 i = 0; i < response_size; i++)
            ((u8 *)response)[i] = source[i];
    }
    return 0;
}

int virtio_gpu_init(void)
{
    u16 port = 0;
    for (u32 bus = 0; bus < 256 && !port; bus++) {
        for (u32 slot = 0; slot < 32 && !port; slot++) {
            u32 id = pci_config_read32(bus, slot, 0, 0);
            if (id == 0xFFFFFFFFu || id == 0)
                continue;
            u32 vendor = id & 0xFFFF;
            u32 device = id >> 16;
            if (vendor != 0x1AF4)
                continue;
            if (device != 0x1010 && device != 0x0010)
                continue;
            u32 low = pci_config_read32(bus, slot, 0, 0x10);
            if (!(low & 1))
                continue;
            port = (u16)(low & 0xFFF0);
            u32 command = pci_config_read32(bus, slot, 0, 4);
            pci_config_write32(bus, slot, 0, 4, command | 0x5);
        }
    }
    if (!port)
        return -1;

    g_gpu.io_base = port;
    out8(VIRTIO_PCI_STATUS, 0);
    out8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
    out8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    g_gpu.features = in32(VIRTIO_PCI_HOST_FEATURES);
    /* Legacy virtio exposes the low 32 feature bits in one register. */
    out32(VIRTIO_PCI_GUEST_FEATURES, g_gpu.features & VIRTIO_GPU_F_VIRGL);
    out8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                            VIRTIO_STATUS_FEATURES_OK);

    out16(VIRTIO_PCI_QUEUE_SELECT, 0);
    g_gpu.queue_size = in16(VIRTIO_PCI_QUEUE_SIZE);
    if (!g_gpu.queue_size || g_gpu.queue_size > 64)
        return -1;
    g_gpu.queue_phys = pmm_alloc_page();
    g_gpu.request_phys = pmm_alloc_page();
    g_gpu.response_phys = pmm_alloc_page();
    if (!g_gpu.queue_phys || !g_gpu.request_phys || !g_gpu.response_phys)
        return -1;
    buffer_zero((void *)(uintptr_t)g_gpu.queue_phys, 4096);
    buffer_zero((void *)(uintptr_t)g_gpu.request_phys, 4096);
    buffer_zero((void *)(uintptr_t)g_gpu.response_phys, 4096);
    out32(VIRTIO_PCI_QUEUE_PFN, (u32)(g_gpu.queue_phys >> 12));
    out8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                            VIRTIO_STATUS_FEATURES_OK |
                            VIRTIO_STATUS_DRIVER_OK);
    g_gpu.ready = 1;

    struct virtio_gpu_resp_display_info display;
    for (u32 i = 0; i < sizeof(display); i++)
        ((u8 *)&display)[i] = 0;
    if (gpu_command(VIRTIO_GPU_CMD_GET_DISPLAY_INFO, NULL, 0, &display,
                    sizeof(display), 0) != 0)
        console_write("[gpu3d] display info query failed\n");

    struct virtio_gpu_resp_capset_info capset;
    for (u32 i = 0; i < sizeof(capset); i++)
        ((u8 *)&capset)[i] = 0;
    u32 capset_index = 0;
    if (gpu_command(VIRTIO_GPU_CMD_GET_CAPSET_INFO, &capset_index, 4,
                    &capset, sizeof(capset), 0) == 0)
        g_gpu.capset_id = capset.capset_id;

    int virgl = (g_gpu.features & VIRTIO_GPU_F_VIRGL) != 0;
    if (virgl && g_gpu.capset_id == 1) {
        struct virtio_gpu_ctx_create create;
        buffer_zero(&create, sizeof(create));
        create.hdr.type = VIRTIO_GPU_CMD_CTX_CREATE;
        create.nlen = 8;
        for (int i = 0; i < 7; i++)
            create.debug_name[i] = "zhaoxu"[i];
        g_gpu.ctx_id = 1;
        if (gpu_command(VIRTIO_GPU_CMD_CTX_CREATE, &create.nlen,
                        sizeof(create) - sizeof(struct virtio_gpu_ctrl_hdr),
                        NULL, 0, 0) != 0)
            console_write("[gpu3d] context creation failed\n");
        else
            console_write("[gpu3d] virgl 3D context ready\n");
    } else {
        console_write("[gpu3d] host does not expose virgl 3D\n");
    }
    return 0;
}

int gpu3d_available(void)
{
    return g_gpu.ready && g_gpu.ctx_id != 0;
}

/* Create a 3D resource that virgl command buffers can target. */
int gpu3d_create_resource(u32 resource_id, u32 format, u32 bind, u32 width,
                          u32 height, u32 depth)
{
    if (!gpu3d_available())
        return -1;
    struct virtio_gpu_resource_create_3d request;
    buffer_zero(&request, sizeof(request));
    request.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
    request.hdr.ctx_id = g_gpu.ctx_id;
    request.resource_id = resource_id;
    request.format = format;
    request.bind = bind;
    request.width = width;
    request.height = height;
    request.depth = depth;
    request.array_size = 1;
    request.last_level = 0;
    request.nr_samples = 0;
    return gpu_command(VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
                       &request.resource_id,
                       sizeof(request) - sizeof(struct virtio_gpu_ctrl_hdr),
                       NULL, 0, 0);
}

/* Submit a virgl command buffer for the current context. */
int gpu3d_submit(const void *commands, u32 length, u32 fence)
{
    if (!gpu3d_available() || !commands || !length)
        return -1;
    if (length > 4096 - sizeof(struct virtio_gpu_submit_3d))
        return -1;
    u8 request[4096];
    struct virtio_gpu_submit_3d *submit =
        (struct virtio_gpu_submit_3d *)request;
    buffer_zero(request, sizeof(request));
    submit->hdr.type = VIRTIO_GPU_CMD_SUBMIT_3D;
    submit->hdr.ctx_id = g_gpu.ctx_id;
    submit->hdr.flags = fence ? VIRTIO_GPU_FLAG_FENCE : 0;
    submit->hdr.fence_id = fence;
    submit->size = length;
    const u8 *source = commands;
    for (u32 i = 0; i < length; i++)
        request[sizeof(*submit) + i] = source[i];
    return gpu_command(VIRTIO_GPU_CMD_SUBMIT_3D,
                       request + sizeof(struct virtio_gpu_ctrl_hdr),
                       4 + length, NULL, 0, fence);
}
