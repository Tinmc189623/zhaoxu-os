/* Zhaoxu OS - ADE client library (command-stream protocol). */
#include <ade_client.h>
#include <ade_protocol.h>
#include <types.h>

#include "../userlib.h"

static void copy_text(char *destination, const char *source)
{
    int i = 0;
    for (; i + 1 < ADE_TEXT_MAX && source[i]; i++)
        destination[i] = source[i];
    destination[i] = 0;
}

static int send_message(long channel, struct ade_message *message)
{
    long sent = sys_port_send(channel, 0, message, sizeof(*message));
    return sent == (long)sizeof(*message) ? 0 : -1;
}

long ade_connect(void)
{
    for (int i = 0; i < 10000; i++) {
        long channel = sys_port_connect(ADE_PORT_NAME);
        if (channel > 0)
            return channel;
        sys_sleep(1);
    }
    return 0;
}

int ade_create_window(long channel, int width, int height, const char *title,
                      char glyph, u32 accent)
{
    struct ade_message message;
    for (u32 i = 0; i < sizeof(message); i++)
        ((u8 *)&message)[i] = 0;
    message.type = ADE_OP_CREATE;
    message.x = width;
    message.y = height;
    message.extra = (u32)(u8)glyph;
    message.color = accent;
    copy_text(message.text, title);
    return send_message(channel, &message);
}

static int send_rect(long channel, u32 type, int x, int y, int w, int h,
                     u32 radius, u32 color)
{
    struct ade_message message;
    for (u32 i = 0; i < sizeof(message); i++)
        ((u8 *)&message)[i] = 0;
    message.type = type;
    message.x = x;
    message.y = y;
    message.w = w;
    message.h = h;
    message.extra = radius;
    message.color = color;
    return send_message(channel, &message);
}

int ade_clear(long channel, u32 color)
{
    return send_rect(channel, ADE_OP_CLEAR, 0, 0, 0, 0, 0, color);
}

int ade_fill(long channel, int x, int y, int w, int h, u32 color)
{
    return send_rect(channel, ADE_OP_FILL, x, y, w, h, 0, color);
}

int ade_round(long channel, int x, int y, int w, int h, u32 radius,
              u32 color)
{
    return send_rect(channel, ADE_OP_ROUND, x, y, w, h, radius, color);
}

int ade_text_flags(long channel, int x, int y, u32 color, u32 flags,
                   const char *text)
{
    struct ade_message message;
    for (u32 i = 0; i < sizeof(message); i++)
        ((u8 *)&message)[i] = 0;
    message.type = ADE_OP_TEXT;
    message.x = x;
    message.y = y;
    message.color = color;
    message.extra = flags;
    copy_text(message.text, text);
    return send_message(channel, &message);
}

int ade_text(long channel, int x, int y, u32 color, const char *text)
{
    return ade_text_flags(channel, x, y, color, 0, text);
}

int ade_commit(long channel)
{
    struct ade_message message;
    for (u32 i = 0; i < sizeof(message); i++)
        ((u8 *)&message)[i] = 0;
    message.type = ADE_OP_COMMIT;
    return send_message(channel, &message);
}

/* Publish one cursor-zone hint; the display server hit-tests these before
 * falling back to the window's default arrow. */
int ade_cursor(long channel, int x, int y, int w, int h, const char *name)
{
    if (!name)
        return -1;
    struct ade_message message;
    for (u32 i = 0; i < sizeof(message); i++)
        ((u8 *)&message)[i] = 0;
    message.type = ADE_OP_CURSOR;
    message.x = x;
    message.y = y;
    message.w = w;
    message.h = h;
    copy_text(message.text, name);
    return send_message(channel, &message);
}

int ade_poll(long channel, struct ade_message *message)
{
    unsigned long type = 0;
    long length = sys_port_try_recv(channel, &type, message, sizeof(*message));
    return length >= (long)sizeof(*message) ? 1 : 0;
}
