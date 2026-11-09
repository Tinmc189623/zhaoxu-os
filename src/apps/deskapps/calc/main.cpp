/* Zhaoxu OS - calculator application (outside ADE). */
extern "C" {
#include <kprintf.h>
#include <types.h>
}

#include "../../ADE/ade.h"

namespace {

const char *const kLabels[16] = {
    "7", "8", "9", "/",
    "4", "5", "6", "*",
    "1", "2", "3", "-",
    "0", "+", "C", "=",
};

i64 g_value;
i64 g_pending;
char g_op;
bool g_fresh = true;
char g_display[32] = "0";

void update_display(void)
{
    ksnprintf(g_display, sizeof(g_display), "%lld", (long long)g_value);
}

i64 apply(i64 a, i64 b, char op)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return b != 0 ? a / b : 0;
    default: return b;
    }
}

void press(int index)
{
    if (index < 0 || index >= 16)
        return;
    const char *label = kLabels[index];

    if (label[0] >= '0' && label[0] <= '9') {
        if (g_fresh) {
            g_value = 0;
            g_fresh = false;
        }
        g_value = g_value * 10 + (label[0] - '0');
    } else if (label[0] == 'C') {
        g_value = 0;
        g_pending = 0;
        g_op = 0;
        g_fresh = true;
    } else if (label[0] == '=') {
        if (g_op)
            g_value = apply(g_pending, g_value, g_op);
        g_op = 0;
        g_fresh = true;
    } else {
        if (g_op)
            g_value = apply(g_pending, g_value, g_op);
        g_pending = g_value;
        g_op = label[0];
        g_fresh = true;
    }

    update_display();
    ade_invalidate();
}

void button_rect(const struct ade_view *view, int index, int *bx, int *by,
                 int *bw, int *bh)
{
    int pad = ade_ui(16);
    int top = view->y + ade_ui(98);
    int grid_w = view->w - pad * 2;
    int grid_h = view->y + view->h - top - ade_ui(16);
    int cw = grid_w / 4;
    int ch = grid_h / 4;
    *bx = view->x + pad + (index % 4) * cw;
    *by = top + (index / 4) * ch;
    *bw = cw - ade_ui(6);
    *bh = ch - ade_ui(6);
}

void paint(const struct ade_view *view)
{
    int pad = ade_ui(16);
    int dx = view->x + pad;
    int dy = view->y + ade_ui(14);

    ade_round(dx, dy, view->w - pad * 2, ade_ui(62), ade_ui(10), 0xE8F1F8);
    ade_border(dx, dy, view->w - pad * 2, ade_ui(62), 0xB6C8D8);
    int width = ade_text_width(g_display, 2);
    ade_text(g_display, dx + view->w - pad * 2 - width - ade_ui(10),
             dy + ade_ui(8), 0x123A5A, 2);

    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        button_rect(view, i, &bx, &by, &bw, &bh);
        u32 color = i == 15 ? 0x2F7FD0
                            : (i % 4 == 3 || i == 14) ? 0xE3EBF3 : 0xFBFDFF;
        u32 text = i == 15 ? 0xFFFFFF : 0x1B2C3D;
        ade_round(bx, by, bw, bh, ade_ui(8), color);
        ade_border(bx, by, bw, bh, 0xA9BCCB);
        ade_text_center(kLabels[i], bx + bw / 2,
                        by + (bh - ade_line_height()) / 2, text, 1);
    }
}

void click(const struct ade_view *view, int x, int y, int buttons)
{
    (void)buttons;
    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        button_rect(view, i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            press(i);
            return;
        }
    }
}

void key(int code)
{
    static const char *const keys = "789/456*123-0+C=";
    if (code == '\n' || code == '\r') {
        press(15);
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (code == keys[i]) {
            press(i);
            return;
        }
    }
    if (code == 8 || code == 127) {
        g_value /= 10;
        update_display();
        ade_invalidate();
    }
}

void init(void)
{
    g_value = 0;
    g_pending = 0;
    g_op = 0;
    g_fresh = true;
    update_display();
}

} // namespace

extern const struct ade_app calc_app = {
    "calc", "计算器", "=",
    "<svg viewBox=\"0 0 100 100\"><rect x=\"6\" y=\"6\" width=\"88\" "
    "height=\"88\" rx=\"22\" fill=\"#8E64C9\"/><rect x=\"26\" y=\"24\" "
    "width=\"48\" height=\"14\" rx=\"3\" fill=\"#FFFFFF\"/><rect x=\"26\" "
    "y=\"46\" width=\"12\" height=\"10\" rx=\"2\" fill=\"#FFFFFF\"/>"
    "<rect x=\"44\" y=\"46\" width=\"12\" height=\"10\" rx=\"2\" "
    "fill=\"#FFFFFF\"/><rect x=\"62\" y=\"46\" width=\"12\" height=\"10\" "
    "rx=\"2\" fill=\"#FFFFFF\"/><rect x=\"26\" y=\"62\" width=\"12\" "
    "height=\"10\" rx=\"2\" fill=\"#FFFFFF\"/><rect x=\"44\" y=\"62\" "
    "width=\"12\" height=\"10\" rx=\"2\" fill=\"#FFFFFF\"/><rect x=\"62\" "
    "y=\"62\" width=\"12\" height=\"10\" rx=\"2\" fill=\"#FFFFFF\"/>"
    "</svg>",
    0x8E64C9, 400, 500,
    init, paint, key, click, nullptr,
};
