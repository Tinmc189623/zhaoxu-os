#ifndef ZHAOXU_DWM_H
#define ZHAOXU_DWM_H

#include "../ade.h"
#include <render.h>
#include <types.h>

#define DWM_MAX_WINDOWS 8

struct dwm_window {
    const struct ade_app *app;
    int x, y, w, h;
    int open;
    int z;
    int drag_dx, drag_dy;
};

struct dwm_ops {
    struct render_surface *wallpaper;
    int menu_h;
    int dock_h;
    void (*paint_menubar)(void);
    void (*paint_dock)(void);
    int (*dock_click)(int x, int y);
    int (*dock_hover)(int x, int y);
    void (*tick)(void);
    void (*poll)(void);
};

void dwm_init(const struct dwm_ops *ops);
struct dwm_window *dwm_open(const struct ade_app *app);
void dwm_invalidate_all(void);
void dwm_invalidate_rect(int x, int y, int w, int h);
void dwm_invalidate_focus(void);
void dwm_invalidate_app(const struct ade_app *app);
void dwm_close_app(const struct ade_app *app);
void dwm_run(void) __attribute__((noreturn));

int dwm_title_height(void);
struct render_rect dwm_content_rect(const struct dwm_window *window);
int dwm_ui(int value);

#endif
