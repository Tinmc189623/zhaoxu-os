/*
 * Zhaoxu OS - graphical installer client for the Ring3 ADE server.
 *
 * Only appears when no ZHAOXUDATA volume is mounted and a blank install
 * target is attached, i.e. when the machine was booted from the ISO.
 */
#include <ade_client.h>
#include <ade_protocol.h>
#include <userlib.h>

static int contains(const char *text, const char *needle)
{
    for (const char *p = text; *p; p++) {
        const char *a = p;
        const char *b = needle;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (!*b)
            return 1;
    }
    return 0;
}

static void draw(long channel, int status, int gpt)
{
    ade_clear(channel, 0xF7FAFD);
    ade_fill(channel, 0, 0, 620, 72, 0x1E6FB8);
    ade_text(channel, 24, 22, 0xFFFFFF, "安装 Zhaoxu OS");
    ade_text(channel, 24, 96, 0x1B2C3D, "单 ISO 安装，无需额外介质");
    ade_text(channel, 24, 128, 0x51606F, "安装会写入唯一的空白目标盘");
    ade_text(channel, 24, 160, 0x51606F, gpt ? "分区表：GPT" : "分区表：MBR");

    ade_round(channel, 24, 200, 250, 48, 10,
              gpt ? 0x6B7C8C : 0x1E6FB8);
    ade_text(channel, 54, 214, 0xFFFFFF, "使用 MBR 安装");
    ade_round(channel, 300, 200, 250, 48, 10,
              gpt ? 0x1E6FB8 : 0x6B7C8C);
    ade_text(channel, 330, 214, 0xFFFFFF, "使用 GPT 安装");

    if (status == 1)
        ade_text(channel, 24, 286, 0x2E7D32, "安装完成，请重启并从目标盘启动");
    else if (status == -1)
        ade_text(channel, 24, 286, 0xC62828, "安装失败：请确认存在空白目标盘");
    else
        ade_text(channel, 24, 286, 0x51606F, "点击按钮开始安装");
    ade_commit(channel);
}

int user_main(long *stack)
{
    long argc = stack[0];
    const char **argv = (const char **)(stack + 1);
    int force = 0;
    if (argc > 0 && argv[0] && argv[0][0] == 'f')
        force = 1;

    if (!force && sys_data_volume())
        return 0;

    static char disks[1024];
    long count = sys_disks(disks, sizeof(disks) - 1);
    if (count <= 0)
        return 0;
    disks[count] = 0;
    if (!contains(disks, "target"))
        return 0;

    long channel = ade_connect();
    if (channel <= 0)
        return 1;
    if (ade_create_window(channel, 620, 340, "安装", 'I', 0x1E6FB8) != 0)
        return 2;
    u_puts("[install] window ready\n");

    int gpt = 1;
    int status = 0;
    draw(channel, status, gpt);

    for (;;) {
        struct ade_message message;
        if (ade_poll(channel, &message)) {
            if (message.type == ADE_EV_CLOSE)
                break;
            if (message.type == ADE_EV_CLICK && status == 0) {
                if (message.x >= 24 && message.x < 274 &&
                    message.y >= 200 && message.y < 248) {
                    gpt = 0;
                } else if (message.x >= 300 && message.x < 550 &&
                           message.y >= 200 && message.y < 248) {
                    gpt = 1;
                } else {
                    sys_sleep(10);
                    continue;
                }
                draw(channel, 0, gpt);
                u_puts("[install] installing\n");
                long result = sys_install(-1, gpt);
                status = result == 0 ? 1 : -1;
                u_puts(status == 1 ? "[install] complete\n"
                                   : "[install] failed\n");
                draw(channel, status, gpt);
            }
        }
        sys_sleep(10);
    }

    sys_port_close(channel);
    return 0;
}
