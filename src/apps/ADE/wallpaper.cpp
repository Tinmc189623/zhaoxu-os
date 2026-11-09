/* Zhaoxu OS - ADE Aqua wallpaper (desktop chrome, separate module). */
#include "ade_internal.h"

struct render_surface *ade_build_wallpaper(void)
{
    struct render_surface *screen = ade_screen();
    if (!screen)
        return nullptr;

    struct render_surface *wallpaper =
        render_surface_create(screen->width, screen->height);
    if (!wallpaper)
        return nullptr;

    render_set_target(wallpaper);
    render_vgrad(0, 0, screen->width, screen->height, 0x0C2745, 0x8FC6E8);
    render_soft_circle(screen->width / 5, screen->height / 4,
                       screen->width / 3, 0xBFE4FF, 26);
    render_soft_circle(screen->width * 4 / 5, screen->height * 3 / 5,
                       screen->width / 4, 0xFFFFFF, 30);
    render_soft_circle(screen->width / 2,
                       screen->height + ade_ui(120),
                       screen->width / 3, 0x0A2A48, 40);
    render_set_target(screen);
    return wallpaper;
}
