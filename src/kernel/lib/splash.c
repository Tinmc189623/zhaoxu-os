/*
 * Zhaoxu OS - boot animation.
 *
 * Painted straight into the Limine framebuffer while the kernel brings up
 * memory, drivers and the desktop service.  The emblem is a ring of twelve
 * dots with a rotating highlight; the progress bar moves with the boot
 * stages, and the timer interrupt advances the frame so the motion continues
 * while the kernel waits on Ring3 services.
 *
 * The ring and background inside the emblem box are snapshotted once, so a
 * frame only copies that box back and repaints the twelve dots.  Repainting
 * the whole box every frame made the display flicker on slow framebuffers.
 */
#include <fbdraw.h>
#include <mem.h>
#include <percpu.h>
#include <splash.h>
#include <string.h>
#include <timer.h>
#include <types.h>

#define SPLASH_BG_TOP     0x0E1116
#define SPLASH_BG_BOTTOM  0x18202B
#define SPLASH_TEXT       0xEAF2FF
#define SPLASH_SUBTEXT    0x8FA3BF
#define SPLASH_ACCENT     0x4C8DFF
#define SPLASH_ACCENT_DIM 0x3E5678
#define SPLASH_TRACK      0x33415A
#define SPLASH_RING       0x2E425E

#define SPLASH_FRAME_TICKS 80     /* one animation frame every 80 ms */
#define SPLASH_DOTS        12
#define SPLASH_DRIP_CAP    99

static struct fb_draw g_fb;
static int g_active;
static int g_drawing;   /* set while a frame is being painted */
static u32 g_percent;
static u32 g_target;
static u32 g_frame;
static u64 g_last_tick;
static u64 g_stage_tick;      /* when the current target was announced */
static u32 g_drip_seconds;    /* whole seconds already credited */
static u32 g_centre_x;
static u32 g_ring_y;
static u32 g_radius;
static u32 g_bar_x;
static u32 g_bar_y;
static u32 g_bar_width;
static u32 g_bar_height;
static char g_label[48];

/* Snapshot of the emblem box (background + ring) and its geometry. */
static u32 *g_emblem_bg;
static u32 g_emblem_x;
static u32 g_emblem_y;
static u32 g_emblem_size;

/* Integer sine/cosine table for 12 steps (roughly unit circle). */
static const int g_cosine[SPLASH_DOTS] = {
    1000, 866, 500, 0, -500, -866,
    -1000, -866, -500, 0, 500, 866,
};
static const int g_sine[SPLASH_DOTS] = {
    0, 500, 866, 1000, 866, 500,
    0, -500, -866, -1000, -866, -500,
};

/* Colour mixing helper for the progress highlight. */
static u32 mix(u32 from, u32 to, u32 amount)
{
    u32 red = ((from >> 16) & 0xFF) * (255 - amount) / 255 +
              ((to >> 16) & 0xFF) * amount / 255;
    u32 green = ((from >> 8) & 0xFF) * (255 - amount) / 255 +
                ((to >> 8) & 0xFF) * amount / 255;
    u32 blue = (from & 0xFF) * (255 - amount) / 255 +
               (to & 0xFF) * amount / 255;
    return (red << 16) | (green << 8) | blue;
}

/* Background colour of one screen row in the splash gradient. */
static u32 gradient_at(u32 screen_y)
{
    u32 amount = g_fb.height > 1 ? screen_y * 255u / (g_fb.height - 1) : 0;
    return mix(SPLASH_BG_TOP, SPLASH_BG_BOTTOM, amount);
}

/* Erase a rectangle back to the background gradient. */
static void erase_rect(u32 x, u32 y, u32 width, u32 height)
{
    for (u32 row = 0; row < height; row++) {
        u32 screen_y = y + row;
        if (screen_y >= g_fb.height)
            break;
        fb_draw_rect(&g_fb, x, screen_y, width, 1, gradient_at(screen_y));
    }
}

/* Draw the background and the static ring into the emblem box, then keep a
 * copy of the box so later frames only repaint the dots. */
static void build_emblem(void)
{
    erase_rect(g_emblem_x, g_emblem_y, g_emblem_size, g_emblem_size);

    u32 inner = (g_radius - 2) * (g_radius - 2);
    u32 outer = (g_radius + 2) * (g_radius + 2);
    for (u32 y = g_emblem_y; y < g_emblem_y + g_emblem_size; y++) {
        int dy = (int)y - (int)g_ring_y;
        for (u32 x = g_emblem_x; x < g_emblem_x + g_emblem_size; x++) {
            int dx = (int)x - (int)g_centre_x;
            u32 distance = (u32)(dx * dx + dy * dy);
            if (distance >= inner && distance <= outer)
                fb_draw_pixel(&g_fb, x, y, SPLASH_RING);
        }
    }

    if (g_emblem_x + g_emblem_size > g_fb.width ||
        g_emblem_y + g_emblem_size > g_fb.height)
        return;      /* too close to an edge: keep the full-redraw path */
    if (!g_emblem_bg)
        g_emblem_bg = kmalloc((usize)g_emblem_size * g_emblem_size * 4);
    if (!g_emblem_bg)
        return;
    for (u32 row = 0; row < g_emblem_size; row++) {
        const u32 *line = (const u32 *)(g_fb.base +
            (usize)(g_emblem_y + row) * g_fb.pitch + (usize)g_emblem_x * 4);
        memcpy(g_emblem_bg + (usize)row * g_emblem_size, line,
               (usize)g_emblem_size * 4);
    }
}

/* Repaint one dot square over the snapshotted background. */
static void draw_dots(void)
{
    if (g_emblem_bg) {
        for (u32 row = 0; row < g_emblem_size; row++) {
            u32 *line = (u32 *)(g_fb.base +
                (usize)(g_emblem_y + row) * g_fb.pitch +
                (usize)g_emblem_x * 4);
            memcpy(line, g_emblem_bg + (usize)row * g_emblem_size,
                   (usize)g_emblem_size * 4);
        }
    } else {
        erase_rect(g_emblem_x, g_emblem_y, g_emblem_size, g_emblem_size);
        /* Without the snapshot the ring has to be rebuilt as well. */
        u32 inner = (g_radius - 2) * (g_radius - 2);
        u32 outer = (g_radius + 2) * (g_radius + 2);
        for (u32 y = g_emblem_y; y < g_emblem_y + g_emblem_size; y++) {
            int dy = (int)y - (int)g_ring_y;
            for (u32 x = g_emblem_x; x < g_emblem_x + g_emblem_size; x++) {
                int dx = (int)x - (int)g_centre_x;
                u32 distance = (u32)(dx * dx + dy * dy);
                if (distance >= inner && distance <= outer)
                    fb_draw_pixel(&g_fb, x, y, SPLASH_RING);
            }
        }
    }

    for (u32 index = 0; index < SPLASH_DOTS; index++) {
        /* One step every four frames (about three steps per second) keeps
         * the highlight readable instead of strobing. */
        u32 step = (index + SPLASH_DOTS - (g_frame / 4) % SPLASH_DOTS) %
                   SPLASH_DOTS;
        u32 bright = index < 4;
        u32 colour = bright ? SPLASH_ACCENT : SPLASH_ACCENT_DIM;
        u32 dot = bright ? 12 : 8;
        int x = (int)g_centre_x + (int)g_radius * g_cosine[step] / 1000;
        int y = (int)g_ring_y + (int)g_radius * g_sine[step] / 1000;
        if (x < 0 || y < 0)
            continue;
        fb_draw_rect(&g_fb, (u32)x - dot / 2, (u32)y - dot / 2, dot, dot,
                     colour);
    }
}

/* Percentage text laid out without stdio. */
static void format_percent(char *out)
{
    char digits[4];
    u32 count = 0;
    u32 value = g_percent;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(digits));
    u32 used = 0;
    while (count)
        out[used++] = digits[--count];
    out[used++] = '%';
    out[used] = 0;
}

/* Progress bar, its moving highlight, the stage label and the percentage. */
static void draw_bar(void)
{
    u32 filled = g_bar_width * g_percent / 100;

    erase_rect(g_bar_x - 2, g_bar_y - 34, g_bar_width + 4, 48);
    fb_draw_rect(&g_fb, g_bar_x - 2, g_bar_y - 2, g_bar_width + 4,
                 g_bar_height + 4, SPLASH_TRACK);
    fb_draw_rect(&g_fb, g_bar_x, g_bar_y, g_bar_width, g_bar_height,
                 SPLASH_BG_TOP);
    if (filled) {
        fb_draw_rect(&g_fb, g_bar_x, g_bar_y, filled, g_bar_height,
                     SPLASH_ACCENT);
        /* A highlight drifting inside the filled part keeps the bar alive
         * while a boot stage is waiting on Ring3. */
        u32 span = 96 + g_bar_width / 8;
        u32 phase = (g_frame * 3) % (g_bar_width + span);
        for (u32 x = phase > span ? phase - span : 0;
             x < filled && x < phase; x++) {
            u32 amount = 170 - (phase - x) * 170 / span;
            fb_draw_rect(&g_fb, g_bar_x + x, g_bar_y, 1, g_bar_height,
                         mix(SPLASH_ACCENT, SPLASH_TEXT, amount));
        }
    }

    fb_draw_text(&g_fb, g_bar_x, g_bar_y - 30, g_label, 2, SPLASH_SUBTEXT);
    char percent[8];
    format_percent(percent);
    fb_draw_text(&g_fb, g_bar_x + g_bar_width -
                        fb_draw_text_width(percent, 2),
                 g_bar_y - 30, percent, 2, SPLASH_TEXT);
}

/* Move the bar towards the stage target, then keep it creeping so a long
 * wait never looks frozen.  The creep is measured in timer seconds since the
 * stage was announced, not in animation frames, so a sparse frame (or a busy
 * interrupt load) cannot leave the bar sitting on one number. */
static void advance_progress(void)
{
    if (g_percent < g_target) {
        g_percent++;
        return;
    }
    if (g_target >= 100 || g_percent >= SPLASH_DRIP_CAP)
        return;

    u32 hz = timer_hz();
    if (hz == 0)
        hz = 1000;
    u64 seconds = (timer_ticks() - g_stage_tick) / hz;
    if (seconds > g_drip_seconds) {
        g_drip_seconds = (u32)seconds;
        u32 next = g_target + (u32)seconds;
        if (next > SPLASH_DRIP_CAP)
            next = SPLASH_DRIP_CAP;
        if (next > g_percent)
            g_percent = next;
    }
}

void splash_begin(const struct boot_info *boot)
{
    if (!boot)
        return;
    if (!fb_draw_attach(&g_fb, boot))
        return;

    g_active = 1;
    g_percent = 0;
    g_target = 0;
    g_frame = 0;
    g_stage_tick = timer_ticks();
    g_drip_seconds = 0;
    g_last_tick = timer_ticks();
    strcpy(g_label, "Starting");

    fb_draw_gradient(&g_fb, SPLASH_BG_TOP, SPLASH_BG_BOTTOM);
    g_centre_x = g_fb.width / 2;
    g_ring_y = g_fb.height * 2 / 5;
    g_radius = (g_fb.width < g_fb.height ? g_fb.width : g_fb.height) / 9;
    if (g_radius < 60)
        g_radius = 60;

    g_bar_width = g_fb.width / 3;
    if (g_bar_width > 720)
        g_bar_width = 720;
    if (g_bar_width < 240)
        g_bar_width = 240;
    g_bar_x = (g_fb.width - g_bar_width) / 2;
    g_bar_y = g_fb.height * 3 / 4;
    g_bar_height = 10;

    g_emblem_size = g_radius * 2 + 20;
    g_emblem_x = g_centre_x > g_radius + 10 ? g_centre_x - g_radius - 10 : 0;
    g_emblem_y = g_ring_y > g_radius + 10 ? g_ring_y - g_radius - 10 : 0;

    g_drawing = 1;
    build_emblem();
    draw_dots();
    draw_bar();
    g_drawing = 0;
}

void splash_stage(u32 percent, const char *label)
{
    if (!g_active)
        return;
    /* Percentages never move backwards; a stage that names a lower number
     * than the drip has reached only updates the label. */
    if (percent > g_target) {
        g_target = percent > 100 ? 100 : percent;
        g_stage_tick = timer_ticks();
        g_drip_seconds = 0;
    }
    if (label) {
        usize length = strlen(label);
        if (length >= sizeof(g_label))
            length = sizeof(g_label) - 1;
        memcpy(g_label, label, length);
        g_label[length] = 0;
    }
    g_drawing = 1;
    draw_bar();
    g_drawing = 0;
}

/* Timer hook: one frame per SPLASH_FRAME_TICKS, boot CPU only so APs cannot
 * interleave writes into the same scanlines. */
void splash_tick(void)
{
    if (!g_active || g_drawing || cpu_index() != 0)
        return;
    u64 now = timer_ticks();
    if (now - g_last_tick < SPLASH_FRAME_TICKS)
        return;
    g_last_tick = now;
    g_frame++;
    advance_progress();
    g_drawing = 1;
    draw_dots();
    draw_bar();
    g_drawing = 0;
}

void splash_finish(void)
{
    g_active = 0;
}
