#ifndef ZHAOXU_SPLASH_H
#define ZHAOXU_SPLASH_H

#include <boot.h>
#include <types.h>

/* Boot animation painted on the Limine framebuffer until the Ring3 display
 * service takes the screen.  Setup discs show it until the framebuffer text
 * console (fbcon) takes over for the installer.  Entry points are no-ops
 * when the machine has no framebuffer. */
void splash_begin(const struct boot_info *boot);
/* Report a boot stage: advances the spinner and moves the progress bar. */
void splash_stage(u32 percent, const char *label);
/* Timer hook: advances one animation frame every few ticks. */
void splash_tick(void);
/* Stop animating; the last frame stays on screen until Aqua paints. */
void splash_finish(void);

#endif
