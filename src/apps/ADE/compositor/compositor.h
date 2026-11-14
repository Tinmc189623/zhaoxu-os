#ifndef ZHAOXU_DWC_H
#define ZHAOXU_DWC_H

#include <render.h>
#include <types.h>

/* Desktop Window Compositor.
 *
 * Owns the back buffer, the damage-to-framebuffer present path and the
 * cursor.  Cursors come from a PNG + text theme when available; the built-in
 * arrow remains as a fallback for a system without /usr/share/cursors. */
int dwc_init(struct render_surface *screen);
struct render_surface *dwc_screen(void);
struct render_surface *dwc_back(void);
void dwc_present(struct render_rect rect);

void dwc_cursor_set(const char *name);
const char *dwc_cursor_theme(void);
int dwc_cursor_animate(void);
void dwc_cursor_restore(void);
void dwc_cursor_draw(int x, int y);
void dwc_present_cursor(int x, int y);
void dwc_hide_cursor(void);
int dwc_cursor_presented(void);
int dwc_cursor_x(void);
int dwc_cursor_y(void);

#endif
