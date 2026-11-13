/* Zhaoxu OS - ADE default wallpaper (rule 33: 3pty/img0.jpg).
 *
 * The build converts the image into a raw 1920x1080 RGB888 frame
 * (tools/wallpaper-raw.rb). The installed system keeps that frame as
 * /WALLPAPER.RAW on the data volume; ADE reads it through zos_load_wallpaper
 * and scales it with integer nearest-neighbour sampling. Without a data
 * volume the Aqua gradient remains as the fallback wallpaper.
 */
#include "ade_internal.h"
#include "zos_user.h"

namespace {
constexpr int kWallpaperWidth = 1920;
constexpr int kWallpaperHeight = 1080;
}

static bool scale_wallpaper(struct render_surface *surface)
{
    u64 bytes = 0;
    const u8 *source = zos_load_wallpaper(&bytes);
    if (!source || bytes < (u64)kWallpaperWidth * kWallpaperHeight * 3)
        return false;

    u32 *pixels = surface->pixels;
    int stride = surface->stride;
    int width = surface->width;
    int height = surface->height;
    for (int y = 0; y < height; y++) {
        int sy = (int)((long)y * kWallpaperHeight / height);
        const u8 *row = source + (long)sy * kWallpaperWidth * 3;
        u32 *line = pixels + (long)y * stride;
        for (int x = 0; x < width; x++) {
            int sx = (int)((long)x * kWallpaperWidth / width);
            const u8 *pixel = row + sx * 3;
            line[x] = ((u32)pixel[0] << 16) | ((u32)pixel[1] << 8) |
                      (u32)pixel[2];
        }
    }
    return true;
}

struct render_surface *ade_build_wallpaper(void)
{
    struct render_surface *screen = ade_screen();
    if (!screen)
        return nullptr;

    struct render_surface *wallpaper =
        render_surface_create(screen->width, screen->height);
    if (!wallpaper)
        return nullptr;

    if (scale_wallpaper(wallpaper))
        return wallpaper;

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
