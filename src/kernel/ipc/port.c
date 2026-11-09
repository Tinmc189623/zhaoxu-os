/*
 * Zhaoxu OS - named ports and channels.
 *
 * A service listens on a well-known name.  Each connect() creates a private
 * channel with one queue per direction, so replies can never be mistaken for
 * requests.  Messages are copied through the kernel; the display protocol
 * keeps bulk draw commands in shared memory and only sends small control
 * messages through a channel.
 */
#include <ipc.h>
#include <kprintf.h>
#include <mem.h>
#include <process.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <types.h>

#define MAX_PORTS 16
#define MAX_CHANNELS 32
#define IPC_TYPE_CONNECT 0xFFFFFFFFFFFFFFFFULL

struct ipc_message {
    struct ipc_message *next;
    u64 type;
    u32 length;
    u8 data[IPC_MSG_MAX];
};

struct ipc_queue {
    struct ipc_message *head;
    struct ipc_message *tail;
    u32 count;
};

struct ipc_port {
    int used;
    char name[IPC_NAME_MAX];
    u64 owner;
    struct ipc_queue pending;
};

struct ipc_channel {
    int used;
    u64 id;
    u64 client;
    u64 server;
    int client_closed;
    int server_closed;
    struct ipc_queue to_server;
    struct ipc_queue to_client;
};

static struct ipc_port g_ports[MAX_PORTS];
static struct ipc_channel g_channels[MAX_CHANNELS];
static spinlock_t g_ipc_lock = SPINLOCK_INIT;
static u64 g_next_channel = 1;

static struct ipc_port *port_at(u64 handle)
{
    u64 index = handle - 1;
    if (handle == 0 || index >= MAX_PORTS || !g_ports[index].used)
        return NULL;
    return &g_ports[index];
}

static struct ipc_channel *channel_find(u64 id)
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (g_channels[i].used && g_channels[i].id == id)
            return &g_channels[i];
    }
    return NULL;
}

static void queue_push(struct ipc_queue *queue, struct ipc_message *message)
{
    message->next = NULL;
    if (queue->tail)
        queue->tail->next = message;
    else
        queue->head = message;
    queue->tail = message;
    queue->count++;
}

static struct ipc_message *queue_pop(struct ipc_queue *queue)
{
    struct ipc_message *message = queue->head;
    if (!message)
        return NULL;
    queue->head = message->next;
    if (!queue->head)
        queue->tail = NULL;
    queue->count--;
    message->next = NULL;
    return message;
}

static void queue_clear(struct ipc_queue *queue)
{
    struct ipc_message *message;
    while ((message = queue_pop(queue)) != NULL)
        kfree(message);
}

static void channel_release(struct ipc_channel *channel)
{
    queue_clear(&channel->to_server);
    queue_clear(&channel->to_client);
    memset(channel, 0, sizeof(*channel));
}

static void channel_close_side(struct ipc_channel *channel, u64 pid)
{
    if (channel->client == pid)
        channel->client_closed = 1;
    if (channel->server == pid)
        channel->server_closed = 1;
    if (channel->client_closed && channel->server_closed)
        channel_release(channel);
}

u64 ipc_listen(const char *name)
{
    if (!name)
        return 0;
    u64 pid = process_self_pid();
    if (!pid)
        return 0;

    u64 flags = spin_lock_irqsave(&g_ipc_lock);

    struct ipc_port *free_slot = NULL;
    for (int i = 0; i < MAX_PORTS; i++) {
        struct ipc_port *port = &g_ports[i];
        if (port->used && strcmp(port->name, name) == 0) {
            u64 handle = port->owner == pid ? (u64)(i + 1) : 0;
            spin_unlock_irqrestore(&g_ipc_lock, flags);
            return handle;
        }
        if (!port->used && !free_slot)
            free_slot = port;
    }
    if (!free_slot) {
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return 0;
    }

    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->used = 1;
    free_slot->owner = pid;
    strncpy(free_slot->name, name, IPC_NAME_MAX - 1);

    u64 handle = (u64)(free_slot - g_ports) + 1;
    spin_unlock_irqrestore(&g_ipc_lock, flags);
    return handle;
}

u64 ipc_connect(const char *name)
{
    if (!name)
        return 0;
    u64 pid = process_self_pid();
    if (!pid)
        return 0;

    u64 flags = spin_lock_irqsave(&g_ipc_lock);

    struct ipc_port *port = NULL;
    for (int i = 0; i < MAX_PORTS; i++) {
        if (g_ports[i].used && strcmp(g_ports[i].name, name) == 0) {
            port = &g_ports[i];
            break;
        }
    }
    if (!port || port->owner == pid) {
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return 0;
    }

    struct ipc_channel *slot = NULL;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (!g_channels[i].used) {
            slot = &g_channels[i];
            break;
        }
    }
    if (!slot) {
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return 0;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->id = g_next_channel++;
    slot->client = pid;
    slot->server = port->owner;

    struct ipc_message *notice = kmalloc(sizeof(*notice));
    if (!notice) {
        memset(slot, 0, sizeof(*slot));
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return 0;
    }
    memset(notice, 0, sizeof(*notice));
    notice->type = IPC_TYPE_CONNECT;
    notice->length = sizeof(u64);
    memcpy(notice->data, &slot->id, sizeof(u64));
    queue_push(&port->pending, notice);

    u64 id = slot->id;
    spin_unlock_irqrestore(&g_ipc_lock, flags);
    return id;
}

int ipc_try_accept(u64 handle, u64 *channel_out)
{
    u64 pid = process_self_pid();
    if (!pid || !channel_out)
        return -1;

    u64 flags = spin_lock_irqsave(&g_ipc_lock);
    struct ipc_port *port = port_at(handle);
    if (!port || port->owner != pid) {
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return -1;
    }

    struct ipc_message *notice = queue_pop(&port->pending);
    if (!notice) {
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return 0;
    }

    u64 channel = 0;
    if (notice->length == sizeof(u64))
        memcpy(&channel, notice->data, sizeof(u64));
    struct ipc_channel *slot = channel_find(channel);
    int valid = slot && slot->server == pid;
    spin_unlock_irqrestore(&g_ipc_lock, flags);
    kfree(notice);

    if (!valid)
        return 0;
    *channel_out = channel;
    return 1;
}

u64 ipc_accept(u64 handle)
{
    for (;;) {
        u64 channel = 0;
        int status = ipc_try_accept(handle, &channel);
        if (status > 0)
            return channel;
        if (status < 0)
            return 0;
        thread_sleep_ms(1);
    }
}

static struct ipc_queue *send_queue(struct ipc_channel *channel, u64 pid)
{
    if (channel->client == pid)
        return &channel->to_server;
    if (channel->server == pid)
        return &channel->to_client;
    return NULL;
}

static struct ipc_queue *recv_queue(struct ipc_channel *channel, u64 pid)
{
    if (channel->server == pid)
        return &channel->to_server;
    if (channel->client == pid)
        return &channel->to_client;
    return NULL;
}

int ipc_send(u64 id, u64 type, const void *data, u32 length)
{
    if (length > IPC_MSG_MAX)
        return -1;
    if (length && !data)
        return -1;
    u64 pid = process_self_pid();
    if (!pid)
        return -1;

    struct ipc_message *message = kmalloc(sizeof(*message));
    if (!message)
        return -1;
    memset(message, 0, sizeof(*message));
    message->type = type;
    message->length = length;
    if (length)
        memcpy(message->data, data, length);

    u64 flags = spin_lock_irqsave(&g_ipc_lock);
    struct ipc_channel *channel = channel_find(id);
    struct ipc_queue *queue = channel ? send_queue(channel, pid) : NULL;
    if (!queue || queue->count >= IPC_QUEUE_DEPTH) {
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        kfree(message);
        return -1;
    }
    queue_push(queue, message);
    spin_unlock_irqrestore(&g_ipc_lock, flags);
    return (int)length;
}

int ipc_try_recv(u64 id, u64 *type, void *data, u32 capacity)
{
    u64 pid = process_self_pid();
    if (!pid)
        return -1;
    if (capacity > IPC_MSG_MAX)
        capacity = IPC_MSG_MAX;

    u64 flags = spin_lock_irqsave(&g_ipc_lock);
    struct ipc_channel *channel = channel_find(id);
    if (!channel) {
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return -1;
    }

    struct ipc_queue *queue = recv_queue(channel, pid);
    int closed = (channel->server == pid) ? channel->client_closed
                                          : channel->server_closed;
    if (!queue) {
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return -1;
    }

    struct ipc_message *message = queue_pop(queue);
    spin_unlock_irqrestore(&g_ipc_lock, flags);
    if (!message)
        return closed ? -1 : 0;

    if (type)
        *type = message->type;
    u32 length = message->length;
    if (length > capacity)
        length = capacity;
    if (length && data)
        memcpy(data, message->data, length);
    kfree(message);
    return (int)length;
}

int ipc_recv(u64 id, u64 *type, void *data, u32 capacity)
{
    for (;;) {
        int length = ipc_try_recv(id, type, data, capacity);
        if (length != 0)
            return length;
        thread_sleep_ms(1);
    }
}

int ipc_close(u64 handle)
{
    u64 pid = process_self_pid();
    if (!pid)
        return -1;

    u64 flags = spin_lock_irqsave(&g_ipc_lock);

    struct ipc_channel *channel = channel_find(handle);
    if (channel) {
        channel_close_side(channel, pid);
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return 0;
    }

    struct ipc_port *port = port_at(handle);
    if (port && port->owner == pid) {
        queue_clear(&port->pending);
        memset(port, 0, sizeof(*port));
        spin_unlock_irqrestore(&g_ipc_lock, flags);
        return 0;
    }

    spin_unlock_irqrestore(&g_ipc_lock, flags);
    return -1;
}

void ipc_process_exit(u64 pid)
{
    if (!pid)
        return;

    u64 flags = spin_lock_irqsave(&g_ipc_lock);

    for (int i = 0; i < MAX_CHANNELS; i++) {
        struct ipc_channel *channel = &g_channels[i];
        if (!channel->used)
            continue;
        if (channel->client == pid || channel->server == pid)
            channel_close_side(channel, pid);
    }

    for (int i = 0; i < MAX_PORTS; i++) {
        struct ipc_port *port = &g_ports[i];
        if (port->used && port->owner == pid) {
            queue_clear(&port->pending);
            memset(port, 0, sizeof(*port));
        }
    }

    spin_unlock_irqrestore(&g_ipc_lock, flags);
}
