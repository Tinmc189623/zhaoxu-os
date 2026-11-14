/* Zhaoxu OS - ADE Aqua dock (desktop chrome, separate module). */
#include "ade_internal.h"
#include "winmgr/dwm.h"

namespace {

int g_hover = -1;

void metrics(int *dx, int *dy, int *dw, int *dh, int *spacing, int *icon)
{
    struct render_surface *screen = ade_screen();
    int count = ade_app_count();
    *icon = ade_ui(46);
    *spacing = *icon + ade_ui(18);
    *dw = *spacing * (count > 0 ? count : 1) + ade_ui(24);
    *dh = ade_ui(78);
    *dx = (screen->width - *dw) / 2;
    *dy = screen->height - ade_dock_height() + ade_ui(8);
}

} // namespace

int ade_dock_height(void)
{
    return ade_ui(92);
}

void ade_dock_paint(void)
{
    int dx, dy, dw, dh, spacing, icon;
    metrics(&dx, &dy, &dw, &dh, &spacing, &icon);

    render_rounded_rect(dx + 3, dy + 5, dw, dh, ade_ui(20), 0x0A1A2A, 90);
    render_rounded_rect(dx, dy, dw, dh, ade_ui(20), 0xF4FAFF, 205);
    render_rounded_rect(dx + 2, dy + 2, dw - 4, dh / 2, ade_ui(18), 0xFFFFFF,
                        90);
    render_border(dx + 1, dy + 1, dw - 2, dh - 2, 0x7E93A8);

    int count = ade_app_count();
    for (int i = 0; i < count; i++) {
        const struct ade_app *app = ade_app_at(i);
        int cx = dx + ade_ui(12) + spacing * i + icon / 2;
        int base_y = dy + dh - icon - ade_ui(21);
        int draw = icon;
        int lift = 0;
        if (i == g_hover) {
            draw = icon * 5 / 4;
            lift = ade_ui(9);
        }
        int ix = cx - draw / 2;
        int iy = base_y - lift + (icon - draw) / 2;

        render_rounded_rect(ix + 2, iy + 3, draw, draw, draw / 5, 0x0A1A2A,
                            60);
        if (app->icon_svg) {
            render_svg(app->icon_svg, ix, iy, draw);
        } else {
            render_rounded_rect(ix, iy, draw, draw, draw / 5, app->accent,
                                255);
            render_rounded_rect(ix + 2, iy + 2, draw - 4, draw / 2,
                                draw / 6, 0xFFFFFF, 65);
            const char *glyph = app->glyph ? app->glyph : "?";
            render_text_center(glyph, cx,
                               iy + (draw - render_line_height()) / 2,
                               0xFFFFFF, draw >= ade_ui(40) ? 2 : 1);
        }

        if (i == g_hover) {
            int label_w = render_text_width(app->name, 1);
            render_text(app->name, cx - label_w / 2,
                        dy + dh - render_line_height() - ade_ui(1),
                        0x1B344C, 1);
        }
    }
}

/* Return the app index under the pointer, including the lift strip above
 * the panel, or -1 outside every icon slot. */
int ade_dock_hit(int x, int y)
{
    int dx, dy, dw, dh, spacing, icon;
    metrics(&dx, &dy, &dw, &dh, &spacing, &icon);
    int count = ade_app_count();
    if (count <= 0 || x < dx || x >= dx + dw || y < dy - ade_ui(20) ||
        y >= dy + dh)
        return -1;
    int index = (x - dx - ade_ui(12)) / spacing;
    if (index < 0 || index >= count)
        return -1;
    return index;
}

int ade_dock_hover(int x, int y)
{
    int dx, dy, dw, dh, spacing, icon;
    metrics(&dx, &dy, &dw, &dh, &spacing, &icon);
    int index = ade_dock_hit(x, y);
    if (index != g_hover) {
        g_hover = index;
        dwm_invalidate_rect(dx - ade_ui(10), dy - ade_ui(28), dw + ade_ui(20),
                            dh + ade_ui(38));
        return 1;
    }
    return 0;
}

int ade_dock_click(int x, int y)
{
    int dx, dy, dw, dh, spacing, icon;
    metrics(&dx, &dy, &dw, &dh, &spacing, &icon);
    if (x < dx || y < dy || x >= dx + dw || y >= dy + dh)
        return 0;
    int index = (x - dx - ade_ui(12)) / spacing;
    if (index < 0 || index >= ade_app_count())
        return 0;
    ade_open_app(ade_app_at(index));
    return 1;
}
