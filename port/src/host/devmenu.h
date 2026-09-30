/* Dev menu (the shared port dev-menu spec, DEV_MENU_SPEC.md).
 *
 * Opt-in only (MM_DEV_MENU=1 or [dev] dev_menu = on); MM_CHEATS=0 disables
 * it completely. Host-only: drawn over the presented frame, it never
 * touches guest memory. The root always lists Warp, Finish Area, Cheats
 * and Options; a page shows only verified entries, and an empty page shows
 * no selectable rows. The simulation keeps running while it is open. */
#ifndef MM_DEVMENU_H
#define MM_DEVMENU_H

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { DM_UP, DM_DOWN, DM_OK, DM_BACK } dm_nav_t;

typedef struct {
    bool ff_on;
    int ff_speed;  /* index: 0 = 2x, 1 = 4x, 2 = 8x, 3 = max */
    int aspect;    /* 0 = 8:7, 1 = square */
    int crop;      /* 0 = none, 1 = 8 lines top and bottom */
    bool show_help;
    /* one-shot requests; the host clears them after acting */
    bool req_screenshot, req_quick_save, req_quick_load;
} dm_state_t;

typedef struct devmenu devmenu_t;

devmenu_t *devmenu_create(dm_state_t *st, void (*log)(const char *line));
void devmenu_free(devmenu_t *m);
void devmenu_toggle(devmenu_t *m);
bool devmenu_is_open(const devmenu_t *m);
void devmenu_nav(devmenu_t *m, dm_nav_t n);
/* A result line shown at the bottom of the menu (and logged). */
void devmenu_toast(devmenu_t *m, const char *fmt, ...);
/* Draws the menu (if open), the help page (if on) and the toast. */
void devmenu_draw(devmenu_t *m, SDL_Renderer *r, SDL_Rect game);

extern const int dm_ff_speeds[4]; /* frames per presented frame; 0 = max */

#endif
