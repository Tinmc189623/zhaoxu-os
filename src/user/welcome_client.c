/* Zhaoxu OS - Welcome client for the Ring3 ADE display server. */
#include <ade_client.h>
#include <ade_protocol.h>
#include <userlib.h>

static void draw(long channel, int clicked)
{
    ade_clear(channel, 0xF7FAFD);
    ade_fill(channel, 0, 0, 640, 72, 0x1E6FB8);
    ade_text(channel, 24, 22, 0xFFFFFF, "欢迎使用 Zhaoxu OS");
    ade_text(channel, 24, 96, 0x1B2C3D, "Ring3 客户端 · ADE");
    ade_text(channel, 24, 136, 0x51606F, "端口 + 命令协议解耦");
    ade_fill(channel, 24, 180, 592, 1, 0xD7E3EF);
    ade_text(channel, 24, 200, 0x36495C, "字体与渲染由系统提供");
    ade_text(channel, 24, 236, 0x36495C, "点击窗口测试事件路由");
    ade_round(channel, 24, 280, 180, 44, 10, 0x1E6FB8);
    ade_text(channel, 54, 290, 0xFFFFFF, "确定");
    if (clicked)
        ade_text(channel, 24, 348, 0x2E7D32, "已收到点击事件");
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

    if (ade_create_window(channel, 640, 460, "欢迎") != 0) {
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
