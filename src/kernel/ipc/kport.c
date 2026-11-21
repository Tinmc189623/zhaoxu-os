/*
 * Zhaoxu OS - kernel-space IPC endpoints (M1).
 *
 * The Ring3 port layer keys channels on process ids.  A personality runs in
 * kernel context without a process, so this file owns a separate endpoint
 * pair with the same message shape (a type plus up to IPC_MSG_MAX bytes).
 * Queues are per direction, so a reply can never be mistaken for a request.
 */
#include <ipc.h>
#include <mem.h>
#include <personality.h>
#include <spinlock.h>
#include <string.h>
#include <thread.h>
#include <types.h>

#define KPORT_MAX_ENDPOINTS 16
#define KPORT_MAX_CHANNELS  8
#define KPORT_QUEUE_DEPTH   32

struct kport_message {
    struct kport_message *next;
    u64 type;
    u32 length;
    u8 data[IPC_MSG_MAX];
};

struct kport_queue {
    struct kport_message *head;
    struct kport_message *tail;
    u32 count;
};

struct kport_channel {
    struct kport_queue queues[2];   /* queues[side]: messages for that side */
    u8 closed[2];
    u8 used;
};

struct kport_endpoint {
    struct kport_channel *channel;
    u8 side;
    u8 used;
};

static struct kport_channel g_channels[KPORT_MAX_CHANNELS];
static struct kport_endpoint g_endpoints[KPORT_MAX_ENDPOINTS];
static spinlock_t g_kport_lock = SPINLOCK_INIT;

static struct kport_endpoint *endpoint_at(u64 handle)
{
    u64 index = handle - 1;

    if (handle == 0 || index >= KPORT_MAX_ENDPOINTS ||
        !g_endpoints[index].used)
        return NULL;
    return &g_endpoints[index];
}

static void queue_push(struct kport_queue *queue, struct kport_message *message)
{
    message->next = NULL;
    if (queue->tail)
        queue->tail->next = message;
    else
        queue->head = message;
    queue->tail = message;
    queue->count++;
}

static struct kport_message *queue_pop(struct kport_queue *queue)
{
    struct kport_message *message = queue->head;

    if (!message)
        return NULL;
    queue->head = message->next;
    if (!queue->head)
        queue->tail = NULL;
    queue->count--;
    message->next = NULL;
    return message;
}

static void queue_clear(struct kport_queue *queue)
{
    struct kport_message *message;

    while ((message = queue_pop(queue)) != NULL)
        kfree(message);
}

static void channel_release(struct kport_channel *channel)
{
    queue_clear(&channel->queues[0]);
    queue_clear(&channel->queues[1]);
    memset(channel, 0, sizeof(*channel));
}

/* Create a connected endpoint pair.  Returns one handle and fills *peer. */
u64 personality_ipc_create(u64 *peer)
{
    if (!peer)
        return 0;

    u64 flags = spin_lock_irqsave(&g_kport_lock);
    struct kport_channel *channel = NULL;
    struct kport_endpoint *first = NULL;
    struct kport_endpoint *second = NULL;

    for (u32 i = 0; i < KPORT_MAX_CHANNELS; i++) {
        if (!g_channels[i].used) {
            channel = &g_channels[i];
            break;
        }
    }

    for (u32 i = 0; i < KPORT_MAX_ENDPOINTS; i++) {
        if (!g_endpoints[i].used) {
            if (!first) {
                first = &g_endpoints[i];
            } else {
                second = &g_endpoints[i];
                break;
            }
        }
    }

    if (!channel || !first || !second) {
        spin_unlock_irqrestore(&g_kport_lock, flags);
        return 0;
    }

    memset(channel, 0, sizeof(*channel));
    channel->used = 1;

    memset(first, 0, sizeof(*first));
    first->channel = channel;
    first->side = 0;
    first->used = 1;

    memset(second, 0, sizeof(*second));
    second->channel = channel;
    second->side = 1;
    second->used = 1;

    u64 handle = (u64)(first - g_endpoints) + 1;
    u64 peer_handle = (u64)(second - g_endpoints) + 1;

    spin_unlock_irqrestore(&g_kport_lock, flags);
    *peer = peer_handle;
    return handle;
}

/* Queue one message on the peer side; fails when the queue is full or closed. */
int personality_ipc_send(u64 endpoint, u64 type, const void *data, u32 length)
{
    if (length > IPC_MSG_MAX || (length && !data))
        return -1;

    struct kport_message *message = kmalloc(sizeof(*message));
    if (!message)
        return -1;

    memset(message, 0, sizeof(*message));
    message->type = type;
    message->length = length;
    if (length)
        memcpy(message->data, data, length);

    u64 flags = spin_lock_irqsave(&g_kport_lock);
    struct kport_endpoint *ep = endpoint_at(endpoint);

    if (!ep || !ep->channel) {
        spin_unlock_irqrestore(&g_kport_lock, flags);
        kfree(message);
        return -1;
    }

    u8 target = (u8)(ep->side ^ 1u);
    struct kport_queue *queue = &ep->channel->queues[target];

    if (ep->channel->closed[target] || queue->count >= KPORT_QUEUE_DEPTH) {
        spin_unlock_irqrestore(&g_kport_lock, flags);
        kfree(message);
        return -1;
    }

    queue_push(queue, message);
    spin_unlock_irqrestore(&g_kport_lock, flags);
    return (int)length;
}

/* Receive on one endpoint; returns full message length, 0 on timeout. */
int personality_ipc_recv(u64 endpoint, u64 *type, void *data, u32 capacity,
                         u32 timeout_ms)
{
    if (capacity > IPC_MSG_MAX)
        capacity = IPC_MSG_MAX;

    u32 waited = 0;

    for (;;) {
        u64 flags = spin_lock_irqsave(&g_kport_lock);
        struct kport_endpoint *ep = endpoint_at(endpoint);

        if (!ep || !ep->channel) {
            spin_unlock_irqrestore(&g_kport_lock, flags);
            return -1;
        }

        struct kport_channel *channel = ep->channel;
        struct kport_message *message = queue_pop(&channel->queues[ep->side]);
        int closed = channel->closed[ep->side ^ 1u];
        spin_unlock_irqrestore(&g_kport_lock, flags);

        if (message) {
            u32 full = message->length;
            u32 copied = full > capacity ? capacity : full;

            if (type)
                *type = message->type;
            if (copied && data)
                memcpy(data, message->data, copied);
            kfree(message);
            return (int)full;
        }

        if (closed)
            return -1;
        if (waited >= timeout_ms)
            return 0;

        thread_sleep_ms(1);
        waited++;
    }
}

/* Close one endpoint; the channel is released when both sides are closed. */
int personality_ipc_close(u64 endpoint)
{
    u64 flags = spin_lock_irqsave(&g_kport_lock);
    struct kport_endpoint *ep = endpoint_at(endpoint);

    if (!ep || !ep->channel) {
        spin_unlock_irqrestore(&g_kport_lock, flags);
        return -1;
    }

    struct kport_channel *channel = ep->channel;

    channel->closed[ep->side] = 1;
    ep->used = 0;
    ep->channel = NULL;

    if (channel->closed[0] && channel->closed[1])
        channel_release(channel);

    spin_unlock_irqrestore(&g_kport_lock, flags);
    return 0;
}

u32 personality_ipc_live(void)
{
    u32 live = 0;

    for (u32 i = 0; i < KPORT_MAX_ENDPOINTS; i++) {
        if (__atomic_load_n(&g_endpoints[i].used, __ATOMIC_RELAXED))
            live++;
    }
    return live;
}
