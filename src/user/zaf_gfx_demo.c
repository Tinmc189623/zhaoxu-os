/*
 * ZAF graphics smoke demo (milestone Z0).
 *
 * Maps the system framebuffer, draws a short animated scene through the
 * backend-neutral ZAF interface, then reports frames, elapsed time and FPS on
 * the serial port. Exit code 0 means every primitive ran without faults.
 */
#include "userlib.h"
#include "zaf_gfx.h"

#define DEMO_FRAMES 48

static void put(const char *text)
{
    long length = 0;
    while (text[length])
        length++;
    sys_serial_write(text, (unsigned long)length);
}

static void putn(long value)
{
    char buffer[24];
    int i = 0;
    if (value < 0) {
        put("-");
        value = -value;
    }
    if (!value) {
        put("0");
        return;
    }
    while (value > 0 && i < (int)sizeof(buffer)) {
        buffer[i++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (i > 0) {
        char one[2] = { buffer[--i], 0 };
        put(one);
    }
}

static zaf_u32 hue(int step)
{
    static const zaf_u32 palette[6] = {
        0x1E6FB8, 0x2E9BD6, 0x5BC8B0, 0x8E7CC3, 0xD98E5A, 0xE05860
    };
    return palette[step % 6];
}

int user_main(long *stack)
{
    (void)stack;

    struct zos_fb_info info;
    long address = sys_fb_map(&info);
    if (address < 0 || !info.address) {
        put("[zaf] no framebuffer; skip\n");
        return 1;
    }

    struct zaf_surface surface;
    int stride = (int)info.stride * 4;
    if (zaf_surface_attach(&surface, (void *)info.address, (int)info.width,
                           (int)info.height, stride,
                           ZAF_FMT_XRGB8888) != 0) {
        put("[zaf] surface attach failed\n");
        return 2;
    }

    put("[zaf] surface ");
    putn(surface.width);
    put("x");
    putn(surface.height);
    put(" stride ");
    putn(surface.stride);
    put("\n");

    zaf_u8 alpha[48 * 48];
    for (int i = 0; i < 48 * 48; i++)
        alpha[i] = (zaf_u8)(255 - (i % 48) * 5);

    long start = sys_uptime();
    int x0[3], y0[3];
    for (int frame = 0; frame < DEMO_FRAMES; frame++) {
        zaf_clear(&surface, 0x0B1A2A);

        for (int band = 0; band < 12; band++) {
            zaf_fill_rect(&surface, 0, band * (surface.height / 12),
                          surface.width, surface.height / 12 + 1,
                          hue(band));
        }
        zaf_fill_round_rect(&surface, 60 + frame * 6, 80, 260, 160, 28,
                            0xF7FAFD);
        zaf_fill_round_rect(&surface, 80 + frame * 6, 100, 220, 120, 20,
                            hue(frame / 8));

        int cx = surface.width / 2;
        int cy = surface.height / 2 + 80;
        int spread = 90 + frame;
        x0[0] = cx;
        y0[0] = cy - spread;
        x0[1] = cx + spread;
        y0[1] = cy + spread;
        x0[2] = cx - spread;
        y0[2] = cy + spread;
        zaf_fill_polygon(&surface, x0, y0, 3, 0xFFD166);
        zaf_blit_alpha(&surface, 40, surface.height - 140, 48, 48, alpha,
                       0xFFFFFF, 200);

        zaf_draw_line(&surface, 0, 0, surface.width - 1, surface.height - 1,
                      0x334455);
        zaf_draw_line(&surface, surface.width - 1, 0, 0, surface.height - 1,
                      0x334455);
    }
    long elapsed = sys_uptime() - start;

    put("[zaf] frames=");
    putn(DEMO_FRAMES);
    put(" elapsed_ms=");
    putn(elapsed);
    put(" fps=");
    if (elapsed > 0)
        putn((long)DEMO_FRAMES * 1000 / elapsed);
    else
        put("n/a");
    put("\n");
    return 0;
}
