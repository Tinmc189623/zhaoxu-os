/* Zhaoxu OS - Welcome client for the ADE display server.
 *
 * Rule 57: the window explains the desktop only; it carries no OS name,
 * version, kernel, team or brand. */
#include <ade_client.h>
#include <ade_protocol.h>
#include <userlib.h>

static void draw(long channel, int clicked)
{
    ade_clear(channel, 0xF7FAFD);
    ade_fill(channel, 0, 0, 640, 72, 0x1E6FB8);
    ade_text(channel, 24, 22, 0xFFFFFF, "欢迎");
    ade_text(channel, 24, 96, 0x1B2C3D, "桌面已经就绪");
    ade_text(channel, 24, 136, 0x51606F, "点击下方按钮开始使用");
    ade_fill(channel, 24, 180, 592, 1, 0xD7E3EF);
    ade_round(channel, 24, 220, 180, 44, 10, 0x1E6FB8);
    ade_text(channel, 54, 230, 0xFFFFFF, "确定");
    ade_cursor(channel, 24, 220, 180, 44, "hand");
    if (clicked)
        ade_text(channel, 24, 288, 0x2E7D32, "已收到点击事件");
    ade_commit(channel);
}

int user_main(long *stack)
{
    (void)stack;

    long channel = ade_connect();
    if (channel <= 0) {
        u_puts("[welcome] ADE not available\n");
        return 1;
    }

    if (ade_create_window(channel, 640, 460, "欢迎", 'W', 0x1E6FB8) != 0) {
        u_puts("[welcome] create window failed\n");
        return 2;
    }

    int clicked = 0;
    draw(channel, clicked);

    for (;;) {
        struct ade_message message;
        if (ade_poll(channel, &message)) {
            if (message.type == ADE_EV_CLOSE)
                break;
            if (message.type == ADE_EV_CLICK) {
                clicked = 1;
                draw(channel, clicked);
            }
        }
        sys_sleep(10);
    }

    sys_port_close(channel);
    return 0;
}
