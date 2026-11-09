/* Zhaoxu OS - Calculator client for the Ring3 ADE display server. */
#include <ade_client.h>
#include <ade_protocol.h>
#include <userlib.h>

#define WIN_W 400
#define WIN_H 500
#define PAD 16
#define GRID_TOP 98
#define CELL_W 92
#define CELL_H 87
#define BTN_W (CELL_W - 6)
#define BTN_H (CELL_H - 6)

static const char *const kLabels[16] = {
    "7", "8", "9", "/",
    "4", "5", "6", "*",
    "1", "2", "3", "-",
    "0", "+", "C", "=",
};

static long long g_value;
static long long g_pending;
static char g_op;
static int g_fresh = 1;
static char g_display[24] = "0";

static void format_value(void)
{
    long long value = g_value;
    unsigned long long magnitude = value < 0 ? (unsigned long long)(-value)
                                             : (unsigned long long)value;
    char digits[24];
    int count = 0;

    if (magnitude == 0)
        digits[count++] = '0';
    while (magnitude) {
        digits[count++] = (char)('0' + (int)(magnitude % 10));
        magnitude /= 10;
    }

    int at = 0;
    if (value < 0)
        g_display[at++] = '-';
    while (count > 0)
        g_display[at++] = digits[--count];
    g_display[at] = 0;
}

static long long apply(long long a, long long b, char op)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b != 0 ? a / b : 0;
    default: return b;
    }
}

static void press(int index)
{
    if (index < 0 || index >= 16)
        return;
    const char *label = kLabels[index];

    if (label[0] >= '0' && label[0] <= '9') {
        if (g_fresh) {
            g_value = 0;
            g_fresh = 0;
        }
        g_value = g_value * 10 + (label[0] - '0');
    } else if (label[0] == 'C') {
        g_value = 0;
        g_pending = 0;
        g_op = 0;
        g_fresh = 1;
    } else if (label[0] == '=') {
        if (g_op)
            g_value = apply(g_pending, g_value, g_op);
        g_op = 0;
        g_fresh = 1;
    } else {
        if (g_op)
            g_value = apply(g_pending, g_value, g_op);
        g_pending = g_value;
        g_op = label[0];
        g_fresh = 1;
    }
    format_value();
}

static void button_rect(int index, int *bx, int *by, int *bw, int *bh)
{
    *bx = PAD + (index % 4) * CELL_W;
    *by = GRID_TOP + (index / 4) * CELL_H;
    *bw = BTN_W;
    *bh = BTN_H;
}

static void draw(long channel)
{
    ade_clear(channel, 0xF4F7FA);

    ade_round(channel, PAD, 14, WIN_W - PAD * 2, 62, 10, 0xB6C8D8);
    ade_round(channel, PAD + 1, 15, WIN_W - PAD * 2 - 2, 60, 9, 0xE8F1F8);
    ade_text_flags(channel, WIN_W - PAD - 10, 22, 0x123A5A,
                   ADE_TEXT_RIGHT | ADE_TEXT_SCALE2, g_display);

    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        button_rect(i, &bx, &by, &bw, &bh);
        u32 color = i == 15 ? 0x2F7FD0
                            : (i % 4 == 3 || i == 14) ? 0xE3EBF3 : 0xFBFDFF;
        u32 text = i == 15 ? 0xFFFFFF : 0x1B2C3D;

        ade_round(channel, bx, by, bw, bh, 8, 0xA9BCCB);
        ade_round(channel, bx + 1, by + 1, bw - 2, bh - 2, 7, color);
        ade_text_flags(channel, bx + bw / 2, by + bh / 2 - 12, text,
                       ADE_TEXT_CENTER, kLabels[i]);
    }

    ade_commit(channel);
}

static void handle_click(long channel, int x, int y)
{
    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        button_rect(i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            press(i);
            draw(channel);
            return;
        }
    }
}

static void handle_key(long channel, int code)
{
    static const char *const keys = "789/456*123-0+C=";
    if (code == '\n' || code == '\r') {
        press(15);
        draw(channel);
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (code == keys[i]) {
            press(i);
            draw(channel);
            return;
        }
    }
    if (code == 8 || code == 127) {
        g_value /= 10;
        format_value();
        draw(channel);
    }
}

int user_main(long *stack)
{
    (void)stack;

    long channel = ade_connect();
    if (channel <= 0) {
        u_puts("[calc] ADE not available\n");
        return 1;
    }
    if (ade_create_window(channel, WIN_W, WIN_H, "计算器", '=', 0x8E64C9) != 0)
        return 2;

    format_value();
    draw(channel);

    for (;;) {
        struct ade_message message;
        if (ade_poll(channel, &message)) {
            if (message.type == ADE_EV_CLOSE)
                break;
            if (message.type == ADE_EV_CLICK)
                handle_click(channel, message.x, message.y);
            else if (message.type == ADE_EV_KEY)
                handle_key(channel, (int)message.extra);
        }
        sys_sleep(10);
    }

    sys_port_close(channel);
    return 0;
}
