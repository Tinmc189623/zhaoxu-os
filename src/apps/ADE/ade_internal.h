#ifndef ZHAOXU_ADE_INTERNAL_H
#define ZHAOXU_ADE_INTERNAL_H

#include "ade.h"
#include <render.h>

struct render_surface *ade_screen(void);
struct render_surface *ade_build_wallpaper(void);

int ade_menubar_height(void);
void ade_menubar_paint(void);
void ade_menubar_tick(void);

int ade_dock_height(void);
void ade_dock_paint(void);
int ade_dock_click(int x, int y);
int ade_dock_hover(int x, int y);

#endif
