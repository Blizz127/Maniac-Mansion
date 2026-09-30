#include "devmenu.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font8x8.h"

const int dm_ff_speeds[4] = {2, 4, 8, 0};
static const char *ff_labels[4] = {"2X", "4X", "8X", "MAX"};
static const char *aspect_labels[2] = {"8:7", "SQUARE"};
static const char *crop_labels[2] = {"NONE", "8 LINES"};

typedef enum { IT_PAGE, IT_TOGGLE, IT_CYCLE, IT_ACTION, IT_NOTE } item_kind_t;
typedef enum { PG_ROOT, PG_WARP, PG_FINISH, PG_CHEATS, PG_OPTIONS, PG_COUNT } page_t;

typedef struct {
    item_kind_t kind;
    const char *label;
    page_t page;             /* IT_PAGE */
    bool *toggle;            /* IT_TOGGLE */
    int *cycle, ncycle;      /* IT_CYCLE */
    const char **cycle_labels;
    bool *request;           /* IT_ACTION */
} item_t;

struct devmenu {
    dm_state_t *st;
    void (*log)(const char *);
    bool open;
    page_t page, stack[8];
    int depth, sel[PG_COUNT];
    char toast[96];
    uint32_t toast_until;
    SDL_Texture *font;
    SDL_Renderer *font_owner;
};

static const char *page_title[PG_COUNT] = {"DEV MENU", "WARP", "FINISH AREA", "CHEATS", "OPTIONS"};

static int page_items(devmenu_t *m, page_t p, item_t *out)
{
    dm_state_t *s = m->st;
    int n = 0;
    switch (p) {
    case PG_ROOT:
        out[n++] = (item_t){.kind = IT_PAGE, .label = "WARP", .page = PG_WARP};
        out[n++] = (item_t){.kind = IT_PAGE, .label = "FINISH AREA", .page = PG_FINISH};
        out[n++] = (item_t){.kind = IT_PAGE, .label = "CHEATS", .page = PG_CHEATS};
        out[n++] = (item_t){.kind = IT_PAGE, .label = "OPTIONS", .page = PG_OPTIONS};
        break;
    case PG_WARP:
        out[n++] = (item_t){.kind = IT_NOTE, .label = "NO VERIFIED WARP SPOTS YET."};
        break;
    case PG_FINISH:
        out[n++] = (item_t){.kind = IT_NOTE, .label = "NO VERIFIED STEPS OR AREAS YET."};
        break;
    case PG_CHEATS:
        out[n++] = (item_t){.kind = IT_NOTE, .label = "NO VERIFIED CHEATS YET."};
        break;
    case PG_OPTIONS:
        out[n++] = (item_t){.kind = IT_TOGGLE, .label = "FAST-FORWARD", .toggle = &s->ff_on};
        out[n++] = (item_t){.kind = IT_CYCLE, .label = "FAST-FORWARD SPEED", .cycle = &s->ff_speed, .ncycle = 4,
                            .cycle_labels = ff_labels};
        out[n++] = (item_t){.kind = IT_ACTION, .label = "SCREENSHOT", .request = &s->req_screenshot};
        out[n++] = (item_t){.kind = IT_ACTION, .label = "QUICK SAVE", .request = &s->req_quick_save};
        out[n++] = (item_t){.kind = IT_ACTION, .label = "QUICK LOAD", .request = &s->req_quick_load};
        out[n++] = (item_t){.kind = IT_CYCLE, .label = "PIXEL ASPECT", .cycle = &s->aspect, .ncycle = 2,
                            .cycle_labels = aspect_labels};
        out[n++] = (item_t){.kind = IT_CYCLE, .label = "OVERSCAN CROP", .cycle = &s->crop, .ncycle = 2,
                            .cycle_labels = crop_labels};
        out[n++] = (item_t){.kind = IT_TOGGLE, .label = "HELP PAGE", .toggle = &s->show_help};
        break;
    default:
        break;
    }
    return n;
}

static bool selectable(const item_t *it) { return it->kind != IT_NOTE; }

devmenu_t *devmenu_create(dm_state_t *st, void (*log)(const char *line))
{
    devmenu_t *m = calloc(1, sizeof *m);
    m->st = st;
    m->log = log;
    return m;
}

void devmenu_free(devmenu_t *m)
{
    if (m && m->font)
        SDL_DestroyTexture(m->font);
    free(m);
}

void devmenu_toast(devmenu_t *m, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m->toast, sizeof m->toast, fmt, ap);
    va_end(ap);
    m->toast_until = SDL_GetTicks() + 3000;
    char line[160];
    snprintf(line, sizeof line, "[DEV_MENU] %s", m->toast);
    if (m->log)
        m->log(line);
}

void devmenu_toggle(devmenu_t *m)
{
    m->open = !m->open;
    if (m->open) {
        m->page = PG_ROOT;
        m->depth = 0;
    }
    if (m->log)
        m->log(m->open ? "[DEV_MENU] open" : "[DEV_MENU] close");
}

bool devmenu_is_open(const devmenu_t *m) { return m->open; }

void devmenu_nav(devmenu_t *m, dm_nav_t n)
{
    if (!m->open)
        return;
    item_t items[16];
    int count = page_items(m, m->page, items);
    int *sel = &m->sel[m->page];
    bool any = false;
    for (int i = 0; i < count; i++)
        any |= selectable(&items[i]);
    if (*sel >= count || (any && !selectable(&items[*sel])))
        *sel = 0;
    switch (n) {
    case DM_UP:
    case DM_DOWN:
        if (!any)
            break;
        do
            *sel = (*sel + (n == DM_UP ? count - 1 : 1)) % count;
        while (!selectable(&items[*sel]));
        break;
    case DM_BACK:
        if (m->depth == 0)
            devmenu_toggle(m);
        else
            m->page = m->stack[--m->depth];
        break;
    case DM_OK: {
        if (!any)
            break;
        item_t *it = &items[*sel];
        char line[160];
        switch (it->kind) {
        case IT_PAGE:
            if (m->depth < 8) {
                m->stack[m->depth++] = m->page;
                m->page = it->page;
            }
            break;
        case IT_TOGGLE:
            *it->toggle = !*it->toggle;
            snprintf(line, sizeof line, "%s: %s", it->label, *it->toggle ? "ON" : "OFF");
            devmenu_toast(m, "%s", line);
            break;
        case IT_CYCLE:
            *it->cycle = (*it->cycle + 1) % it->ncycle;
            devmenu_toast(m, "%s: %s", it->label, it->cycle_labels[*it->cycle]);
            break;
        case IT_ACTION:
            *it->request = true; /* the host reports the result as a toast */
            break;
        case IT_NOTE:
            break;
        }
    } break;
    }
}

/* ---- drawing ---- */

static void ensure_font(devmenu_t *m, SDL_Renderer *r)
{
    if (m->font && m->font_owner == r)
        return;
    uint32_t px[128 * 8 * 8];
    for (int c = 0; c < 128; c++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                px[y * 1024 + c * 8 + x] = (font8x8[c][y] >> x) & 1 ? 0xFFFFFFFFu : 0;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, 1024, 8, 32, 1024 * 4, SDL_PIXELFORMAT_ARGB8888);
    m->font = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    SDL_SetTextureBlendMode(m->font, SDL_BLENDMODE_BLEND);
    m->font_owner = r;
}

static void text(devmenu_t *m, SDL_Renderer *r, int x, int y, int sc, uint32_t rgb, const char *s)
{
    SDL_SetTextureColorMod(m->font, rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF);
    for (; *s; s++, x += 8 * sc) {
        unsigned char c = (unsigned char)*s & 0x7F;
        SDL_Rect src = {c * 8, 0, 8, 8}, dst = {x, y, 8 * sc, 8 * sc};
        SDL_RenderCopy(r, m->font, &src, &dst);
    }
}

static void panel(SDL_Renderer *r, SDL_Rect box)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 8, 8, 24, 224);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 200, 200, 255, 255);
    SDL_RenderDrawRect(r, &box);
}

static const char *help_lines[] = {
    "DEV MENU KEYS (OPT-IN)",
    "",
    "F8 / BACK+START  MENU OPEN/CLOSE",
    "UP/DOWN          MOVE",
    "ENTER / A        CONFIRM",
    "ESC / B          BACK",
    "F1               THIS HELP",
    "F6 / L3          FAST-FORWARD",
    "BKSP / R3 (HOLD) FAST-FORWARD",
    "F7               FF SPEED",
    "F10              SCREENSHOT",
    "F11 / F12        QUICK SAVE / LOAD",
    "",
    "THE GAME KEEPS RUNNING WHILE",
    "THE MENU IS OPEN.",
};

void devmenu_draw(devmenu_t *m, SDL_Renderer *r, SDL_Rect g)
{
    bool toast = m->toast[0] && SDL_TICKS_PASSED(m->toast_until, SDL_GetTicks()) == 0;
    if (!m->open && !m->st->show_help && !toast)
        return;
    ensure_font(m, r);
    int sc = g.h / 240;
    if (sc < 1)
        sc = 1;
    int lh = 10 * sc;
    if (m->st->show_help) {
        int n = (int)(sizeof help_lines / sizeof help_lines[0]);
        SDL_Rect box = {g.x + 8 * sc, g.y + 8 * sc, g.w - 16 * sc, (n + 1) * lh};
        panel(r, box);
        for (int i = 0; i < n; i++)
            text(m, r, box.x + 6 * sc, box.y + 5 * sc + i * lh, sc, i ? 0xE0E0E0 : 0xFFD050, help_lines[i]);
    }
    if (m->open && !m->st->show_help) {
        item_t items[16];
        int count = page_items(m, m->page, items);
        int *sel = &m->sel[m->page];
        bool any = false;
        for (int i = 0; i < count; i++)
            any |= selectable(&items[i]);
        if (*sel >= count || (any && !selectable(&items[*sel])))
            *sel = 0;
        SDL_Rect box = {g.x + 16 * sc, g.y + 40 * sc, g.w - 32 * sc, (count + 3) * lh};
        panel(r, box);
        char title[64];
        snprintf(title, sizeof title, "%s", page_title[m->page]);
        text(m, r, box.x + 6 * sc, box.y + 5 * sc, sc, 0xFFD050, title);
        for (int i = 0; i < count; i++) {
            item_t *it = &items[i];
            char row[80];
            switch (it->kind) {
            case IT_PAGE: snprintf(row, sizeof row, "%s >", it->label); break;
            case IT_TOGGLE: snprintf(row, sizeof row, "%s: %s", it->label, *it->toggle ? "ON" : "OFF"); break;
            case IT_CYCLE: snprintf(row, sizeof row, "%s: %s", it->label, it->cycle_labels[*it->cycle]); break;
            default: snprintf(row, sizeof row, "%s", it->label); break;
            }
            int y = box.y + 5 * sc + (i + 2) * lh;
            bool on = any && i == *sel;
            if (on) {
                SDL_Rect hl = {box.x + 2 * sc, y - sc, box.w - 4 * sc, lh};
                SDL_SetRenderDrawColor(r, 60, 60, 140, 255);
                SDL_RenderFillRect(r, &hl);
            }
            text(m, r, box.x + 6 * sc, y, sc, it->kind == IT_NOTE ? 0x9090A0 : (on ? 0xFFFFFF : 0xC8C8D8), row);
        }
    }
    if (toast) {
        int w = (int)strlen(m->toast) * 8 * sc + 12 * sc;
        SDL_Rect box = {g.x + (g.w - w) / 2, g.y + g.h - 24 * sc, w, lh + 4 * sc};
        panel(r, box);
        text(m, r, box.x + 6 * sc, box.y + 3 * sc, sc, 0xFFFFFF, m->toast);
    }
}
