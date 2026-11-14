/*
 * Zhaoxu OS - ADE display server.
 *
 * Clients connect to the "ade" port, create a window and submit a small
 * command stream.  The stream is replayed through the system render API when
 * DWM needs the window content; ADE never touches application pages itself.
 */
extern "C" {
#include <ade_protocol.h>
#include <kprintf.h>
#include <render.h>
#include <string.h>
#include <types.h>
#include <userlib.h>
}

#include "../ade.h"
#include "../ade_internal.h"
#include "../winmgr/dwm.h"
#include "server.h"

namespace {

constexpr int kMaxClients = 8;
constexpr int kMaxCommands = 128;
constexpr int kMaxCursorZones = 16;

struct client_command {
    u32 op;
    i32 x, y, w, h;
    u32 color;
    u32 extra;
    char text[ADE_TEXT_MAX];
};

struct client_cursor_zone {
    int x, y, w, h;
    char name[ADE_TEXT_MAX];
};

struct client {
    int used;
    u64 channel;
    char title[ADE_TEXT_MAX];
    char glyph[2];
    char id[16];
    struct ade_app app;
    struct client_command commands[kMaxCommands];
    int command_count;
    struct client_cursor_zone cursor_zones[kMaxCursorZones];
    int cursor_zone_count;
};

struct client_ops {
    void (*paint)(const struct ade_view *view);
    void (*key)(int key);
    void (*click)(const struct ade_view *view, int x, int y, int buttons);
    const char *(*cursor)(const struct ade_view *view, int x, int y);
    void (*close)(void);
};

client g_clients[kMaxClients];
u64 g_listen_port;

void client_paint(int index, const struct ade_view *view);
void client_key(int index, int key);
void client_click(int index, const struct ade_view *view, int x, int y,
                  int buttons);
const char *client_cursor(int index, const struct ade_view *view, int x,
                          int y);
void client_close(int index);

#define CLIENT_WRAPPERS(n)                                                     \
    void client##n##_paint(const struct ade_view *view)                        \
    {                                                                          \
        client_paint(n, view);                                                 \
    }                                                                          \
    void client##n##_key(int key)                                              \
    {                                                                          \
        client_key(n, key);                                                    \
    }                                                                          \
    void client##n##_click(const struct ade_view *view, int x, int y,          \
                           int buttons)                                        \
    {                                                                          \
        client_click(n, view, x, y, buttons);                                  \
    }                                                                          \
    const char *client##n##_cursor(const struct ade_view *view, int x, int y)  \
    {                                                                          \
        return client_cursor(n, view, x, y);                                   \
    }                                                                          \
    void client##n##_close(void)                                               \
    {                                                                          \
        client_close(n);                                                       \
    }

CLIENT_WRAPPERS(0)
CLIENT_WRAPPERS(1)
CLIENT_WRAPPERS(2)
CLIENT_WRAPPERS(3)
CLIENT_WRAPPERS(4)
CLIENT_WRAPPERS(5)
CLIENT_WRAPPERS(6)
CLIENT_WRAPPERS(7)

const struct client_ops g_client_ops[kMaxClients] = {
    { client0_paint, client0_key, client0_click, client0_cursor,
      client0_close },
    { client1_paint, client1_key, client1_click, client1_cursor,
      client1_close },
    { client2_paint, client2_key, client2_click, client2_cursor,
      client2_close },
    { client3_paint, client3_key, client3_click, client3_cursor,
      client3_close },
    { client4_paint, client4_key, client4_click, client4_cursor,
      client4_close },
    { client5_paint, client5_key, client5_click, client5_cursor,
      client5_close },
    { client6_paint, client6_key, client6_click, client6_cursor,
      client6_close },
    { client7_paint, client7_key, client7_click, client7_cursor,
      client7_close },
};

void send_event(int index, u32 type, int x, int y, u32 extra)
{
    struct client *client = &g_clients[index];
    if (!client->used || !client->channel)
        return;

    struct ade_message message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.x = x;
    message.y = y;
    message.extra = extra;
    sys_port_send((long)client->channel, 0, &message, sizeof(message));
}

void append_command(int index, const struct ade_message *message)
{
    struct client *client = &g_clients[index];
    if (message->type == ADE_OP_CLEAR) {
        client->command_count = 0;
        client->cursor_zone_count = 0;
    }
    if (client->command_count >= kMaxCommands)
        return;

    struct client_command *command =
        &client->commands[client->command_count++];
    command->op = message->type;
    command->x = message->x;
    command->y = message->y;
    command->w = message->w;
    command->h = message->h;
    command->color = message->color;
    command->extra = message->extra;
    strncpy(command->text, message->text, ADE_TEXT_MAX - 1);
    command->text[ADE_TEXT_MAX - 1] = 0;
}

void client_paint(int index, const struct ade_view *view)
{
    const struct client *client = &g_clients[index];
    if (!client->used || !view)
        return;

    for (int i = 0; i < client->command_count; i++) {
        const struct client_command *command = &client->commands[i];
        int x = view->x + command->x;
        int y = view->y + command->y;
        switch (command->op) {
        case ADE_OP_CLEAR:
            render_fill_rect(view->x, view->y, view->w, view->h,
                             command->color);
            break;
        case ADE_OP_FILL:
            render_fill_rect(x, y, command->w, command->h, command->color);
            break;
        case ADE_OP_ROUND:
            render_rounded_rect(x, y, command->w, command->h,
                                command->extra ? (int)command->extra
                                               : 8,
                                command->color, 255);
            break;
        case ADE_OP_TEXT: {
            int scale = (command->extra & ADE_TEXT_SCALE2) ? 2 : 1;
            int width = render_text_width(command->text, scale);
            int draw_x = x;
            if (command->extra & ADE_TEXT_CENTER)
                draw_x = x - width / 2;
            else if (command->extra & ADE_TEXT_RIGHT)
                draw_x = x - width;
            render_text(command->text, draw_x, y, command->color, scale);
            break;
        }
        default:
            break;
        }
    }
}

void client_key(int index, int key)
{
    send_event(index, ADE_EV_KEY, 0, 0, (u32)key);
}

void client_click(int index, const struct ade_view *view, int x, int y,
                  int buttons)
{
    send_event(index, ADE_EV_CLICK, x - view->x, y - view->y, (u32)buttons);
}

/* Last declared zone wins, so a client can layer a "wait" hint over the
 * button zones while a long operation runs. */
const char *client_cursor(int index, const struct ade_view *view, int x,
                          int y)
{
    const struct client *client = &g_clients[index];
    if (!client->used || !view)
        return nullptr;
    int local_x = x - view->x;
    int local_y = y - view->y;
    for (int i = client->cursor_zone_count - 1; i >= 0; i--) {
        const struct client_cursor_zone *zone = &client->cursor_zones[i];
        if (local_x >= zone->x && local_x < zone->x + zone->w &&
            local_y >= zone->y && local_y < zone->y + zone->h)
            return zone->name;
    }
    return nullptr;
}

void append_cursor_zone(int index, const struct ade_message *message)
{
    struct client *client = &g_clients[index];
    if (client->cursor_zone_count >= kMaxCursorZones)
        return;
    struct client_cursor_zone *zone =
        &client->cursor_zones[client->cursor_zone_count++];
    zone->x = message->x;
    zone->y = message->y;
    zone->w = message->w;
    zone->h = message->h;
    strncpy(zone->name, message->text, sizeof(zone->name) - 1);
    zone->name[sizeof(zone->name) - 1] = 0;
}

void client_close(int index)
{
    send_event(index, ADE_EV_CLOSE, 0, 0, 0);
}

void release_client(int index)
{
    struct client *client = &g_clients[index];
    if (!client->used)
        return;
    dwm_close_app(&client->app);
    ade_unregister_app(&client->app);
    kprintf("[aqua] client %d disconnected\n", index);
    memset(client, 0, sizeof(*client));
    dwm_invalidate_all();
}

void handle_create(int index, const struct ade_message *message)
{
    struct client *client = &g_clients[index];
    if (client->app.paint)
        return;

    strncpy(client->title, message->text, ADE_TEXT_MAX - 1);
    client->title[ADE_TEXT_MAX - 1] = 0;
    client->glyph[0] = message->extra ? (char)message->extra : 'A';
    client->glyph[1] = 0;
    ksnprintf(client->id, sizeof(client->id), "client%d", index);

    struct ade_app *app = &client->app;
    memset(app, 0, sizeof(*app));
    app->id = client->id;
    app->name = client->title;
    app->glyph = client->glyph;
    app->accent = message->color ? message->color : 0x1E6FB8;
    app->default_w = message->x > 0 ? message->x : 640;
    app->default_h = message->y > 0 ? message->y : 460;
    app->unscaled = 1;
    app->icon_svg = ade_icon_for_title(client->title);
    app->paint = g_client_ops[index].paint;
    app->key = g_client_ops[index].key;
    app->click = g_client_ops[index].click;
    app->close = g_client_ops[index].close;
    app->cursor_at = g_client_ops[index].cursor;

    ade_register_app(app);
    dwm_open(app);
}

void handle_message(int index, const struct ade_message *message)
{
    switch (message->type) {
    case ADE_OP_CREATE:
        handle_create(index, message);
        break;
    case ADE_OP_CLEAR:
    case ADE_OP_FILL:
    case ADE_OP_ROUND:
    case ADE_OP_TEXT:
        append_command(index, message);
        break;
    case ADE_OP_COMMIT:
        dwm_invalidate_app(&g_clients[index].app);
        break;
    case ADE_OP_CURSOR:
        append_cursor_zone(index, message);
        break;
    default:
        break;
    }
}

} // namespace

void ade_display_init(void)
{
    memset(g_clients, 0, sizeof(g_clients));
    g_listen_port = (u64)sys_port_listen(ADE_PORT_NAME);
    if (!g_listen_port) {
        kprintf("[aqua] display port unavailable\n");
        return;
    }

    long welcome = sys_spawn("/bin/apps/welcome.zxe");
    long calc = sys_spawn("/bin/apps/calc.zxe");
    long installer = sys_spawn("/bin/apps/install.zxe");
    kprintf("[aqua] display server listening, welcome %ld, calc %ld, "
            "install %ld\n",
            welcome, calc, installer);
}

void ade_display_poll(void)
{
    if (g_listen_port) {
        for (;;) {
            unsigned long channel = 0;
            int status = (int)sys_port_try_accept((long)g_listen_port,
                                                   &channel);
            if (status <= 0)
                break;

            int slot = -1;
            for (int i = 0; i < kMaxClients; i++) {
                if (!g_clients[i].used) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                sys_port_close((long)channel);
                continue;
            }
            memset(&g_clients[slot], 0, sizeof(g_clients[slot]));
            g_clients[slot].used = 1;
            g_clients[slot].channel = (u64)channel;
        }
    }

    for (int i = 0; i < kMaxClients; i++) {
        struct client *client = &g_clients[i];
        if (!client->used)
            continue;

        for (int burst = 0; burst < 64; burst++) {
            struct ade_message message;
            unsigned long type = 0;
            long length = sys_port_try_recv((long)client->channel, &type,
                                            &message, sizeof(message));
            if (length == 0)
                break;
            if (length < 0) {
                release_client(i);
                break;
            }
            if (length >= (long)sizeof(message))
                handle_message(i, &message);
        }
    }
}
