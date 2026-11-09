#ifndef ZHAOXU_ADE_H
#define ZHAOXU_ADE_H

#include <render.h>
#include <types.h>

/*
 * Public desktop API.  Applications live outside src/apps/ADE and only use
 * these entry points; ADE never contains an application implementation.
 *
 * Applications draw through the system render API with the clip that DWM
 * installs for their content area.  All coordinates passed to ade_fill() and
 * friends are absolute screen coordinates (use view->x / view->y).
 */

struct ade_view {
    int x;          /* content area, absolute */
    int y;
    int w;
    int h;
    int focused;
};

struct ade_app {
    const char *id;
    const char *name;
    const char *glyph;       /* fallback dock glyph */
    const char *icon_svg;    /* SVG icon, system renderer */
    u32 accent;
    int default_w;
    int default_h;

    void (*init)(void);
    void (*paint)(const struct ade_view *view);
    void (*key)(int key);
    void (*click)(const struct ade_view *view, int x, int y, int buttons);
    void (*tick)(void);
    void (*close)(void);
    int unscaled;            /* client windows use raw pixel sizes */
};

void ade_register_app(const struct ade_app *app);
void ade_unregister_app(const struct ade_app *app);
int ade_app_count(void);
const struct ade_app *ade_app_at(int index);
void ade_open_app(const struct ade_app *app);
void ade_invalidate(void);

/* Drawing helpers routed to the system render API. */
void ade_fill(int x, int y, int w, int h, u32 rgb);
void ade_blend(int x, int y, int w, int h, u32 rgb, u32 alpha);
void ade_round(int x, int y, int w, int h, int radius, u32 rgb);
int ade_text(const char *text, int x, int y, u32 fg, int scale);
int ade_text_width(const char *text, int scale);
int ade_text_center(const char *text, int cx, int y, u32 fg, int scale);
int ade_line_height(void);
int ade_ui(int value);
int ade_screen_width(void);
int ade_screen_height(void);
void ade_hline(int x, int y, int w, u32 rgb);
void ade_vgrad(int x, int y, int w, int h, u32 top, u32 bottom);
void ade_border(int x, int y, int w, int h, u32 rgb);

void ade_register_builtin_apps(void);

#ifdef __cplusplus
extern "C" {
#endif
void ade_run(void) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif

#endif
