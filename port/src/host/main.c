/* maniac-mansion-port: SDL2 host for the Stage 1 NES core.
 *
 * Runs the user's own Maniac Mansion (USA) ROM, verified by SHA-256.
 * Defaults are the original game as it ran on an NTSC console: 8:7 pixel
 * aspect at an integer scale, no filters, no enhancements. */
#include <SDL.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/cart.h"
#include "../core/nes.h"
#include "../core/trace.h"
#include "config.h"
#include "devmenu.h"
#include "pads.h"
#include "palette.h"
#include "png.h"

#ifndef MM_VERSION
#define MM_VERSION "dev"
#endif

#define NTSC_FPS (1789772.7272 * 3.0 / (341.0 * 262.0 - 0.5))

static FILE *log_file;

static void logline(const char *s)
{
    fprintf(stderr, "%s\n", s);
    if (log_file) {
        fprintf(log_file, "%s\n", s);
        fflush(log_file);
    }
}

static void logf_(const char *fmt, ...)
{
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    logline(b);
}

/* ---------------------------------------------------------------- files */

static bool write_atomic(const char *path, const void *data, size_t n)
{
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    return ok && rename(tmp, path) == 0;
}

static char *read_small(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    static char b[1024];
    size_t n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    b[n] = 0;
    char *nl = strchr(b, '\n');
    if (nl)
        *nl = 0;
    return b;
}

/* ------------------------------------------------------------------ ROM */

static const char *remembered_rom_file(void)
{
    static char p[1100];
    snprintf(p, sizeof p, "%s/rom-path", path_data_dir());
    return p;
}

static void remember_rom(const char *path)
{
    char *abs = realpath(path, NULL);
    if (!abs)
        return;
    path_mkdirs(path_data_dir());
    char line[1100];
    int n = snprintf(line, sizeof line, "%s\n", abs);
    write_atomic(remembered_rom_file(), line, (size_t)n);
    free(abs);
}

/* Candidates in order: --rom, MM_ROM / [game] rom, remembered, data dir. */
static bool find_rom(config_t *cfg, const char *cli_rom, cart_t *cart, char *err, size_t errlen, char *used,
                     size_t usedlen, cart_status_t *status, bool *explicit_out)
{
    const char *cands[4];
    int n = 0;
    if (cli_rom)
        cands[n++] = cli_rom;
    const char *c = getenv("MM_ROM");
    if (!c)
        c = config_str(cfg, "game", "rom", NULL);
    if (c && *c)
        cands[n++] = c;
    char *r = read_small(remembered_rom_file());
    static char remembered[1100], local[1100];
    if (r && *r) {
        snprintf(remembered, sizeof remembered, "%s", r);
        cands[n++] = remembered;
    }
    snprintf(local, sizeof local, "%s/rom.nes", path_data_dir());
    cands[n++] = local;

    err[0] = 0;
    *explicit_out = cli_rom || (c && *c);
    for (int i = 0; i < n; i++) {
        bool explicit_choice = i == 0 && *explicit_out;
        FILE *probe = fopen(cands[i], "rb");
        if (!probe && !explicit_choice)
            continue;
        if (probe)
            fclose(probe);
        if (cart_load_status(cart, cands[i], false, err, errlen, status)) {
            snprintf(used, usedlen, "%s", cands[i]);
            return true;
        }
        if (explicit_choice)
            return false; /* the user asked for this file: report why */
    }
    if (!err[0]) {
        snprintf(err, errlen, "no Maniac Mansion (USA) ROM found");
        *status = CART_ERR_MISSING;
    }
    return false;
}

/* ---------------------------------------------------------------- saves */

typedef struct {
    char path[1100];
    uint8_t last[8192];
    uint32_t last_write_ms;
} save_t;

static void save_open(save_t *s, nes_t *nes)
{
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/saves", path_data_dir());
    path_mkdirs(dir);
    snprintf(s->path, sizeof s->path, "%s/maniac-mansion-usa.sav", dir);
    FILE *f = fopen(s->path, "rb");
    if (f) {
        size_t n = fread(nes->prgram, 1, sizeof nes->prgram, f);
        fclose(f);
        logf_("battery save: loaded %s (%zu bytes)", s->path, n);
    } else {
        logf_("battery save: none yet (%s)", s->path);
    }
    memcpy(s->last, nes->prgram, sizeof s->last);
}

static void save_flush(save_t *s, nes_t *nes, bool force)
{
    uint32_t now = SDL_GetTicks();
    if (!force && now - s->last_write_ms < 2000)
        return;
    s->last_write_ms = now;
    if (!memcmp(s->last, nes->prgram, sizeof s->last))
        return;
    if (write_atomic(s->path, nes->prgram, sizeof nes->prgram))
        memcpy(s->last, nes->prgram, sizeof s->last);
    else
        logf_("battery save: cannot write %s: %s", s->path, strerror(errno));
}

/* ---------------------------------------------------------------- input */

typedef struct {
    SDL_Scancode key[8]; /* A B SELECT START UP DOWN LEFT RIGHT */
} keymap_t;

static const char *btn_names[8] = {"a", "b", "select", "start", "up", "down", "left", "right"};

static void keymap_load(keymap_t *km, config_t *cfg)
{
    static const char *defaults[8] = {"X", "Z", "Right Shift", "Return", "Up", "Down", "Left", "Right"};
    for (int i = 0; i < 8; i++) {
        const char *name = config_str(cfg, "input", btn_names[i], defaults[i]);
        SDL_Scancode sc = SDL_GetScancodeFromName(name);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            logf_("config: unknown key '%s' for input.%s; using %s", name, btn_names[i], defaults[i]);
            sc = SDL_GetScancodeFromName(defaults[i]);
        }
        km->key[i] = sc;
    }
}

typedef struct {
    SDL_GameController *gc[PADS_MAX];
    int32_t id[PADS_MAX];
    int n;
    pad_set_t set;
    uint32_t trace_window_ms;
    int trace_lines;
} pads_t;

static uint8_t pad_buttons(SDL_GameController *gc)
{
    uint8_t b = 0;
    const int T = 16000;
    int lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A)) b |= PAD_A;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)) b |= PAD_B;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X)) b |= PAD_B;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK)) b |= PAD_SELECT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) b |= PAD_START;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -T) b |= PAD_UP;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > T) b |= PAD_DOWN;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -T) b |= PAD_LEFT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > T) b |= PAD_RIGHT;
    /* Opposing directions cannot be pressed together on a real pad. */
    if ((b & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN)) b &= ~(PAD_UP | PAD_DOWN);
    if ((b & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) b &= ~(PAD_LEFT | PAD_RIGHT);
    return b;
}

static void pads_open(pads_t *p, int device_index)
{
    if (!SDL_IsGameController(device_index) || p->n == PADS_MAX)
        return;
    SDL_GameController *gc = SDL_GameControllerOpen(device_index);
    if (!gc)
        return;
    int32_t id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    for (int i = 0; i < p->n; i++)
        if (p->id[i] == id) {
            SDL_GameControllerClose(gc);
            return;
        }
    p->gc[p->n] = gc;
    p->id[p->n++] = id;
    const char *name = SDL_GameControllerName(gc);
    uint16_t vid = SDL_GameControllerGetVendor(gc), pid = SDL_GameControllerGetProduct(gc);
    pads_added(&p->set, id, vid, pid, name, pad_buttons(gc) != 0);
    logf_("[DEV_MENU] pad connected: %s vid:pid=%04x:%04x instance=%d%s", name ? name : "?", vid, pid, (int)id,
          pads_is_steam_virtual(vid, pid, name) ? " (steam virtual)" : "");
}

static void pads_close(pads_t *p, int32_t id)
{
    for (int i = 0; i < p->n; i++)
        if (p->id[i] == id) {
            SDL_GameControllerClose(p->gc[i]);
            p->gc[i] = p->gc[--p->n];
            p->id[i] = p->id[p->n];
            pads_removed(&p->set, id);
            logf_("[DEV_MENU] pad removed: instance=%d", (int)id);
            return;
        }
}

/* Game pad state from the active controller (§4a selection). */
static uint8_t pads_poll(pads_t *p, uint8_t *hot)
{
    uint8_t out = 0;
    *hot = 0;
    uint32_t now = SDL_GetTicks();
    if (now - p->trace_window_ms >= 1000) {
        p->trace_window_ms = now;
        p->trace_lines = 0;
    }
    for (int i = 0; i < p->n; i++) {
        uint8_t b = pad_buttons(p->gc[i]);
        bool l3 = SDL_GameControllerGetButton(p->gc[i], SDL_CONTROLLER_BUTTON_LEFTSTICK);
        bool r3 = SDL_GameControllerGetButton(p->gc[i], SDL_CONTROLLER_BUTTON_RIGHTSTICK);
        bool non_neutral = b || l3 || r3;
        if (non_neutral && p->trace_lines < 3) {
            p->trace_lines++;
            logf_("[DEV_MENU] input instance=%d buttons=%02x l3=%d r3=%d", (int)p->id[i], b, l3, r3);
        }
        if (pads_input(&p->set, p->id[i], non_neutral)) {
            out = b;
            *hot = (uint8_t)((l3 ? 1 : 0) | (r3 ? 2 : 0));
        }
    }
    return out;
}

/* ---------------------------------------------------------------- video */

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *frame;  /* 256x240, nearest */
    SDL_Texture *scaled; /* integer prescale, then linear to the window */
    int prescale;
    bool par87;
    int crop; /* lines hidden at top and bottom */
    char window_shot[1024]; /* pending capture of the next presented frame */
    uint32_t lut[512];
} video_t;

static void video_lut(video_t *v)
{
    for (int i = 0; i < 512; i++)
        v->lut[i] = 0xFF000000u | nes_rgb((uint16_t)i);
}

static void video_present(video_t *v, const uint16_t *fb, devmenu_t *dm)
{
    void *pixels;
    int pitch;
    if (SDL_LockTexture(v->frame, NULL, &pixels, &pitch) == 0) {
        for (int y = 0; y < NES_H; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * pitch);
            const uint16_t *src = fb + y * NES_W;
            for (int x = 0; x < NES_W; x++)
                row[x] = v->lut[src[x] & 0x1FF];
        }
        SDL_UnlockTexture(v->frame);
    }
    int ww, wh;
    SDL_GetRendererOutputSize(v->ren, &ww, &wh);
    int vis_h = NES_H - 2 * v->crop;
    double par = v->par87 ? 8.0 / 7.0 : 1.0;
    int scale = wh / vis_h;
    while (scale > 1 && (int)(NES_W * par * scale + 0.5) > ww)
        scale--;
    if (scale < 1)
        scale = 1;
    SDL_Rect dst;
    dst.h = vis_h * scale;
    dst.w = (int)(NES_W * par * scale + 0.5);
    if (dst.w > ww) { /* window smaller than 1x: fit */
        dst.w = ww;
        dst.h = (int)(ww / (NES_W * par) * vis_h);
    }
    dst.x = (ww - dst.w) / 2;
    dst.y = (wh - dst.h) / 2;
    SDL_Rect src = {0, v->crop, NES_W, vis_h};

    SDL_SetRenderDrawColor(v->ren, 0, 0, 0, 255);
    SDL_RenderClear(v->ren);
    if (v->scaled && v->par87) {
        /* Sharp 8:7: nearest integer prescale, linear only for the
         * fractional horizontal stretch. */
        SDL_SetRenderTarget(v->ren, v->scaled);
        SDL_Rect pre = {0, 0, NES_W * v->prescale, vis_h * v->prescale};
        SDL_RenderCopy(v->ren, v->frame, &src, &pre);
        SDL_SetRenderTarget(v->ren, NULL);
        SDL_RenderCopy(v->ren, v->scaled, &pre, &dst);
    } else {
        SDL_RenderCopy(v->ren, v->frame, &src, &dst);
    }
    if (dm)
        devmenu_draw(dm, v->ren, dst); /* host overlay on the presented copy */
    if (v->window_shot[0]) { /* tests: capture exactly what is presented */
        int w, h;
        SDL_GetRendererOutputSize(v->ren, &w, &h);
        uint8_t *rgb = malloc((size_t)w * h * 3);
        if (rgb && SDL_RenderReadPixels(v->ren, NULL, SDL_PIXELFORMAT_RGB24, rgb, w * 3) == 0)
            logf_(png_write_rgb(v->window_shot, rgb, w, h) ? "window shot: %s" : "window shot failed: %s", v->window_shot);
        free(rgb);
        v->window_shot[0] = 0;
    }
    SDL_RenderPresent(v->ren);
}

/* ------------------------------------------------------------- helpers */

static void take_screenshot(const uint16_t *fb, devmenu_t *dm)
{
    char dir[1100], p[1200];
    snprintf(dir, sizeof dir, "%s/screenshots", path_state_dir());
    path_mkdirs(dir);
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(p, sizeof p, "%s/mm-%04d%02d%02d-%02d%02d%02d.png", dir, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    bool ok = png_write_nes(p, fb);
    logf_(ok ? "screenshot: %s" : "screenshot failed: %s", p);
    if (dm)
        devmenu_toast(dm, ok ? "SCREENSHOT SAVED" : "SCREENSHOT FAILED");
}

static const char *quick_slot(void)
{
    static char p[1100];
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/quicksave", path_data_dir());
    path_mkdirs(dir);
    snprintf(p, sizeof p, "%s/slot1.mmst", dir);
    return p;
}

/* Quick save/load are whole-machine snapshots in a host-side slot; the
 * game's own battery save is untouched. */
static void quick_save(nes_t *nes, const uint8_t id[8], devmenu_t *dm)
{
    static uint8_t buf[sizeof(nes_t) + 64];
    nes_state_save(nes, buf, id);
    bool ok = write_atomic(quick_slot(), buf, nes_state_size());
    logf_("[DEV_MENU] quick save %s: %s", ok ? "done" : "FAILED", quick_slot());
    if (dm)
        devmenu_toast(dm, ok ? "QUICK SAVE DONE" : "QUICK SAVE FAILED");
}

static void quick_load(nes_t *nes, const uint8_t id[8], devmenu_t *dm)
{
    static uint8_t buf[sizeof(nes_t) + 64];
    FILE *f = fopen(quick_slot(), "rb");
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f)
        fclose(f);
    const char *why = !f ? "NO QUICK SAVE YET" : nes_state_load(nes, buf, n, id) ? NULL : "QUICK SAVE IS FROM ANOTHER BUILD";
    logf_("[DEV_MENU] quick load %s", why ? why : "done");
    if (dm)
        devmenu_toast(dm, "%s", why ? why : "QUICK LOAD DONE");
}

/* Harness-only scripted dev keys (spec §9): MM_DEV_KEYS="frame:KEY,...",
 * KEY = F8 UP DOWN OK BACK F1 F10 F11 F12. Inert unless set. */
typedef struct {
    long frame;
    char key[8];
} devkey_t;

static int parse_devkeys(devkey_t *out, int max)
{
    const char *s = getenv("MM_DEV_KEYS");
    int n = 0;
    while (s && *s && n < max) {
        long fr;
        char k[8];
        int used = 0;
        if (sscanf(s, "%ld:%7[A-Z0-9]%n", &fr, k, &used) != 2)
            break;
        out[n].frame = fr;
        snprintf(out[n].key, sizeof out[n].key, "%s", k);
        n++;
        s += used;
        if (*s == ',')
            s++;
    }
    return n;
}

/* ----------------------------------------------------------------- main */

static void usage(void)
{
    printf("maniac-mansion-port %s\n"
           "usage: maniac-mansion-port [--rom FILE] [--config FILE] [--set section.key=value]... [--check-rom]\n"
           "\n"
           "Runs your own Maniac Mansion (USA) NES ROM (checked by SHA-256). The ROM is\n"
           "looked for in: --rom, $MM_ROM, [game] rom in config.ini, the last ROM used,\n"
           "then %s/rom.nes.\n"
           "\n"
           "Keys: arrows = D-pad, X = A, Z = B, Enter = Start, Right Shift = Select,\n"
           "      F6 = fast-forward on/off, Backspace (hold) = fast-forward, F10 = screenshot,\n"
           "      Alt+Enter = fullscreen, Esc = quit. Controllers: A = A, B/X = B,\n"
           "      Back = Select, Start = Start, L3 = fast-forward on/off, R3 (hold) = fast-forward.\n",
           MM_VERSION, path_data_dir());
}

static void fatal_box(const char *title, const char *msg)
{
    logf_("error: %s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL);
}

int main(int argc, char **argv)
{
    const char *cli_rom = NULL, *cfg_path = NULL, *trace_path = NULL, *shot_path = NULL, *hash_path = NULL;
    long max_frames = -1, shot_frame = -1;
    struct { long frame; char path[1024]; } wshots[16];
    int nwshots = 0;
    bool check_only = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc)
            cli_rom = argv[++i];
        else if (!strcmp(argv[i], "--config") && i + 1 < argc)
            cfg_path = argv[++i];
        else if (!strcmp(argv[i], "--set") && i + 1 < argc)
            i++; /* applied after the config file is loaded */
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage();
            return 0;
        } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
            trace_path = argv[++i]; /* replay a .mmin (tests, demos) */
        } else if (!strcmp(argv[i], "--hashes") && i + 1 < argc) {
            hash_path = argv[++i]; /* per-frame mmfb1 hashes (tests) */
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            max_frames = strtol(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            char *colon; /* FRAME:PATH */
            shot_frame = strtol(argv[++i], &colon, 10);
            shot_path = *colon == ':' ? colon + 1 : NULL;
        } else if (!strcmp(argv[i], "--window-shot") && i + 1 < argc && nwshots < 16) {
            char *colon; /* FRAME:PATH, the presented window including overlays */
            wshots[nwshots].frame = strtol(argv[++i], &colon, 10);
            if (*colon == ':')
                snprintf(wshots[nwshots++].path, sizeof wshots[0].path, "%s", colon + 1);
        } else if (!strcmp(argv[i], "--check-rom")) {
            check_only = true;
        } else if (!strcmp(argv[i], "--version")) {
            printf("maniac-mansion-port %s\n", MM_VERSION);
            return 0;
        } else if (argv[i][0] != '-' && !cli_rom) {
            cli_rom = argv[i]; /* allow a bare ROM path (drag onto launcher) */
        } else {
            fprintf(stderr, "unknown option '%s' (see --help)\n", argv[i]);
            return 2;
        }
    }

    path_mkdirs(path_state_dir());
    char logp[1100];
    snprintf(logp, sizeof logp, "%s/port.log", path_state_dir());
    log_file = fopen(logp, "w");
    logf_("maniac-mansion-port %s", MM_VERSION);

    char err[768];
    config_t *cfg = config_load(cfg_path, err, sizeof err);
    if (!cfg) {
        fprintf(stderr, "error: %s\n", err);
        return 2;
    }
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--set") && i + 1 < argc) {
            char kv[600];
            snprintf(kv, sizeof kv, "%s", argv[++i]);
            char *eq = strchr(kv, '=');
            if (!eq) {
                fprintf(stderr, "error: --set expects section.key=value, got '%s'\n", kv);
                return 2;
            }
            *eq = 0;
            config_set(cfg, kv, eq + 1);
        }

    cart_t cart;
    char used[1100];
    cart_status_t rom_status = CART_OK;
    bool rom_explicit = false;
    bool have_rom = find_rom(cfg, cli_rom, &cart, err, sizeof err, used, sizeof used, &rom_status, &rom_explicit);
    if (check_only) {
        /* Machine-readable result for launchers; no window, no SDL. */
        if (have_rom) {
            printf("MM_ROM_OK path=%s sha256=%s\n", used, cart.sha256);
            return 0;
        }
        printf("MM_ROM_ERROR code=%d %s\n", rom_status, err);
        return rom_status;
    }

    /* §4a: un-hide physical pads before any SDL init. */
    pads_fix_environment(logline);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "error: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    video_t v = {0};
    int scale = config_int(cfg, "video", "scale", 3);
    v.par87 = strcmp(config_str(cfg, "video", "aspect", "8:7"), "square") != 0;
    v.crop = config_int(cfg, "video", "overscan_crop", 0);
    if (v.crop < 0 || v.crop > 16)
        v.crop = 0;
    bool fullscreen = config_bool(cfg, "video", "fullscreen", false);
    bool vsync = config_bool(cfg, "video", "vsync", true);
    int win_w = (int)(NES_W * (v.par87 ? 8.0 / 7.0 : 1.0) * scale + 0.5), win_h = (NES_H - 2 * v.crop) * scale;
    v.win = SDL_CreateWindow("Maniac Mansion", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                 (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!v.win) {
        fprintf(stderr, "error: cannot open a window: %s\n", SDL_GetError());
        return 1;
    }
    v.ren = SDL_CreateRenderer(v.win, -1, SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!v.ren)
        v.ren = SDL_CreateRenderer(v.win, -1, 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    v.frame = SDL_CreateTexture(v.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, NES_W, NES_H);
    v.prescale = 4;
    v.scaled = SDL_CreateTexture(v.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, NES_W * v.prescale,
                                 NES_H * v.prescale);
    if (v.scaled)
        SDL_SetTextureScaleMode(v.scaled, SDL_ScaleModeLinear);
    video_lut(&v);

    /* ROM: verified by hash. A ROM the user named that fails the check is
     * reported and the port exits with its error class; with no ROM at
     * all, the window waits for one to be dropped on it. */
    if (!have_rom && rom_explicit) {
        char msg[1200];
        snprintf(msg, sizeof msg, "%s.\n\nThis port runs your own Maniac Mansion (USA) NES ROM.", err);
        fprintf(stderr, "MM_ROM_ERROR code=%d %s\n", rom_status, err);
        fatal_box("Maniac Mansion: ROM problem", msg);
        return rom_status;
    }
    if (!have_rom) {
        fprintf(stderr, "MM_ROM_ERROR code=%d %s\n", rom_status, err);
        char msg[1400];
        snprintf(msg, sizeof msg,
                 "%s.\n\nThis port needs your own Maniac Mansion (USA) NES ROM (262,160 bytes, No-Intro).\n"
                 "Drop the .nes file onto the game window, or start with:\n"
                 "  maniac-mansion-port --rom \"/path/to/Maniac Mansion (USA).nes\"\n"
                 "It is remembered for next time.",
                 err);
        fatal_box("Maniac Mansion: ROM needed", msg);
        SDL_SetWindowTitle(v.win, "Maniac Mansion: drop your Maniac Mansion (USA) .nes ROM on this window");
        SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
        while (!have_rom) {
            SDL_Event e;
            if (!SDL_WaitEvent(&e))
                continue;
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                return CART_ERR_MISSING;
            if (e.type == SDL_DROPFILE) {
                if (cart_load(&cart, e.drop.file, false, err, sizeof err)) {
                    snprintf(used, sizeof used, "%s", e.drop.file);
                    have_rom = true;
                } else {
                    fatal_box("Maniac Mansion: wrong file", err);
                }
                SDL_free(e.drop.file);
            }
            SDL_SetRenderDrawColor(v.ren, 16, 16, 24, 255);
            SDL_RenderClear(v.ren);
            SDL_RenderPresent(v.ren);
        }
        SDL_SetWindowTitle(v.win, "Maniac Mansion");
    }
    remember_rom(used);
    logf_("rom: %s (sha256 %s) verified: Maniac Mansion (USA)", used, cart.sha256);

    static nes_t nes;
    int rate = config_int(cfg, "audio", "rate", 48000);
    nes_audio_config(&nes, rate);
    nes_init(&nes, cart.prg, cart.prg_size);
    nes_power(&nes, 0x00);
    save_t save;
    save_open(&save, &nes);

    /* audio */
    bool audio_on = config_bool(cfg, "audio", "enabled", true);
    int volume = config_int(cfg, "audio", "volume", 100);
    SDL_AudioDeviceID adev = 0;
    if (audio_on) {
        SDL_AudioSpec want = {0}, have;
        want.freq = rate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!adev)
            logf_("audio: unavailable (%s); continuing without sound", SDL_GetError());
        else
            SDL_PauseAudioDevice(adev, 0);
    }
    const double base_cps = 1789773.0 / rate;
    const uint32_t target_queue = (uint32_t)(rate / 60 * 3) * 2; /* ~3 frames, bytes */

    keymap_t km;
    keymap_load(&km, cfg);
    pads_t pads = {0};
    pads_init(&pads.set, logline);
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        pads_open(&pads, i);

    trace_t trace = {0};
    if (trace_path && !trace_load(&trace, trace_path, err, sizeof err)) {
        fatal_box("Maniac Mansion", err);
        return 2;
    }
    long frame_no = 0;
    uint64_t emu_ticks = 0;
    bool running = true;
    FILE *hash_out = hash_path ? fopen(hash_path, "w") : NULL;
    uint64_t freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    double acc = 0, frame_time = 1.0 / NTSC_FPS;
    static int16_t abuf[8192];
    uint8_t prev_hot = 0;

    /* dev menu: opt-in; MM_CHEATS=0 disables it entirely */
    const char *cheats_env = getenv("MM_CHEATS");
    const char *dev_env = getenv("MM_DEV_MENU");
    bool dev_on = !(cheats_env && !strcmp(cheats_env, "0")) &&
                  ((dev_env && !strcmp(dev_env, "1")) || config_bool(cfg, "dev", "dev_menu", false));
    dm_state_t dms = {0};
    dms.ff_speed = 1;
    dms.aspect = v.par87 ? 0 : 1;
    dms.crop = v.crop ? 1 : 0;
    devmenu_t *dm = dev_on ? devmenu_create(&dms, logline) : NULL;
    logf_("[DEV_MENU] %s", dev_on ? "enabled (F8 or Back+Start)" : "off");
    uint8_t prg_id[8];
    for (int i = 0; i < 8; i++) {
        unsigned b;
        sscanf(cart.prg_sha256 + i * 2, "%2x", &b);
        prg_id[i] = (uint8_t)b;
    }
    devkey_t devkeys[64];
    int ndevkeys = dev_on ? parse_devkeys(devkeys, 64) : 0, devkey_i = 0;
    bool drain = false;          /* after the menu closes: wait for neutral */
    int combo_hold = 0;          /* frames Back or Start has been held (opt-in) */
    bool combo_fired = false;
    uint8_t prev_nav = 0;        /* pad nav edges: 1 up 2 down 4 ok 8 back */

    while (running) {
        SDL_Event e;
        bool menu = dm && devmenu_is_open(dm);
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_CONTROLLERDEVICEADDED: pads_open(&pads, e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED: pads_close(&pads, e.cdevice.which); break;
            case SDL_KEYDOWN: {
                if (e.key.repeat)
                    break;
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(v.win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                } else if (menu) {
                    /* menu open: it consumes navigation; port hotkeys are suppressed */
                    if (k == SDLK_UP) devmenu_nav(dm, DM_UP);
                    else if (k == SDLK_DOWN) devmenu_nav(dm, DM_DOWN);
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) devmenu_nav(dm, DM_OK);
                    else if (k == SDLK_ESCAPE) devmenu_nav(dm, DM_BACK);
                    else if (k == SDLK_F8) devmenu_toggle(dm);
                    else if (k == SDLK_F1) dms.show_help = !dms.show_help;
                } else if (k == SDLK_ESCAPE) {
                    running = false;
                } else if (k == SDLK_F6) {
                    dms.ff_on = !dms.ff_on;
                } else if (k == SDLK_F10) {
                    take_screenshot(nes.ppu.fb, dm);
                } else if (dm) {
                    if (k == SDLK_F8) devmenu_toggle(dm);
                    else if (k == SDLK_F1) dms.show_help = !dms.show_help;
                    else if (k == SDLK_F7) {
                        dms.ff_speed = (e.key.keysym.mod & KMOD_SHIFT) ? 1 : (dms.ff_speed + 1) % 4;
                        devmenu_toast(dm, "FAST-FORWARD SPEED: %s",
                                      (const char *[]){"2X", "4X", "8X", "MAX"}[dms.ff_speed]);
                    } else if (k == SDLK_F11) dms.req_quick_save = true;
                    else if (k == SDLK_F12) dms.req_quick_load = true;
                }
                if (dm && !devmenu_is_open(dm) && menu)
                    drain = true;
            } break;
            }
        }

        /* scripted dev keys (tests only) */
        while (devkey_i < ndevkeys && devkeys[devkey_i].frame <= frame_no) {
            const char *k = devkeys[devkey_i++].key;
            logf_("[DEV_MENU] scripted key %s at frame %ld", k, frame_no);
            bool was = devmenu_is_open(dm);
            if (!strcmp(k, "F8")) devmenu_toggle(dm);
            else if (!strcmp(k, "UP")) devmenu_nav(dm, DM_UP);
            else if (!strcmp(k, "DOWN")) devmenu_nav(dm, DM_DOWN);
            else if (!strcmp(k, "OK")) devmenu_nav(dm, DM_OK);
            else if (!strcmp(k, "BACK")) devmenu_nav(dm, DM_BACK);
            else if (!strcmp(k, "F1")) dms.show_help = !dms.show_help;
            else if (!strcmp(k, "F10")) take_screenshot(nes.ppu.fb, dm);
            else if (!strcmp(k, "F11")) dms.req_quick_save = true;
            else if (!strcmp(k, "F12")) dms.req_quick_load = true;
            if (was && !devmenu_is_open(dm))
                drain = true;
        }

        /* input */
        const uint8_t *ks = SDL_GetKeyboardState(NULL);
        uint8_t kb = 0;
        for (int i = 0; i < 8; i++)
            if (ks[km.key[i]])
                kb |= (uint8_t)(1 << i);
        if ((kb & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN)) kb &= ~(PAD_UP | PAD_DOWN);
        if ((kb & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) kb &= ~(PAD_LEFT | PAD_RIGHT);
        uint8_t hot;
        uint8_t gp = pads_poll(&pads, &hot);
        menu = dm && devmenu_is_open(dm);

        if (dm) {
            /* Back+Start on the same pad opens/closes the menu. With the
             * opt-in on, both are held back from the game for a short grace
             * window so the combo never also reaches the game. */
            bool combo_now = false;
            for (int i = 0; i < pads.n; i++)
                if (SDL_GameControllerGetButton(pads.gc[i], SDL_CONTROLLER_BUTTON_BACK) &&
                    SDL_GameControllerGetButton(pads.gc[i], SDL_CONTROLLER_BUTTON_START))
                    combo_now = true;
            bool either = gp & (PAD_SELECT | PAD_START);
            if (combo_now && !combo_fired) {
                combo_fired = true;
                bool was = devmenu_is_open(dm);
                devmenu_toggle(dm);
                if (was)
                    drain = true;
                menu = devmenu_is_open(dm);
            }
            if (!either) {
                combo_fired = false;
                combo_hold = 0;
            } else {
                combo_hold++;
            }
            if (combo_fired || (either && combo_hold <= 6))
                gp &= ~(PAD_SELECT | PAD_START);
            /* pad navigation: edges only, no auto-repeat */
            uint8_t nav = 0;
            if (menu && !combo_fired) {
                if (gp & PAD_UP) nav |= 1;
                if (gp & PAD_DOWN) nav |= 2;
                if (gp & PAD_A) nav |= 4;
                if (gp & PAD_B) nav |= 8;
                uint8_t edge = nav & ~prev_nav;
                if (edge & 1) devmenu_nav(dm, DM_UP);
                if (edge & 2) devmenu_nav(dm, DM_DOWN);
                if (edge & 4) devmenu_nav(dm, DM_OK);
                if (edge & 8) {
                    devmenu_nav(dm, DM_BACK);
                    if (!devmenu_is_open(dm))
                        drain = true;
                }
            }
            prev_nav = nav;
            menu = devmenu_is_open(dm);
            /* one-shot requests from the menu or F-keys */
            if (dms.req_screenshot) {
                dms.req_screenshot = false;
                take_screenshot(nes.ppu.fb, dm);
            }
            if (dms.req_quick_save) {
                dms.req_quick_save = false;
                quick_save(&nes, prg_id, dm);
            }
            if (dms.req_quick_load) {
                dms.req_quick_load = false;
                quick_load(&nes, prg_id, dm);
            }
            v.par87 = dms.aspect == 0;
            v.crop = dms.crop ? 8 : 0;
        }
        if ((hot & 1) && !(prev_hot & 1) && !menu)
            dms.ff_on = !dms.ff_on; /* L3 */
        prev_hot = hot;
        bool ff = !menu && (dms.ff_on || ks[SDL_SCANCODE_BACKSPACE] || (hot & 2));

        uint8_t game_in = kb | gp;
        if (menu)
            game_in = 0; /* the menu consumes input; the game keeps running */
        if (drain) {
            if (game_in)
                game_in = 0; /* held from the menu: must return to neutral first */
            else
                drain = false;
        }
        if (trace_path) {
            uint8_t tp[2];
            trace_at(&trace, (uint32_t)frame_no, tp);
            nes_set_pad(&nes, 0, tp[0]);
            nes_set_pad(&nes, 1, tp[1]);
        } else {
            nes_set_pad(&nes, 0, game_in);
        }

        /* timing */
        uint64_t now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / (double)freq;
        last = now;
        if (acc > 0.25)
            acc = frame_time; /* after a stall, don't try to catch up */
        int to_run = 0;
        uint64_t budget = 0;
        if (ff) {
            int sp = dm_ff_speeds[dms.ff_speed];
            to_run = sp ? sp : 64;
            budget = sp ? 0 : now + freq / 70; /* max: whatever fits in ~14 ms */
            acc = 0;
        } else {
            while (acc >= frame_time && to_run < 3) {
                acc -= frame_time;
                to_run++;
            }
        }
        for (int f = 0; f < to_run; f++) {
            if (trace_path && f > 0) {
                uint8_t tp[2]; /* traces advance one frame at a time */
                trace_at(&trace, (uint32_t)frame_no, tp);
                nes_set_pad(&nes, 0, tp[0]);
                nes_set_pad(&nes, 1, tp[1]);
            }
            uint64_t t0 = SDL_GetPerformanceCounter();
            nes_run_frame(&nes);
            emu_ticks += SDL_GetPerformanceCounter() - t0;
            if (frame_no == shot_frame && shot_path)
                logf_(png_write_nes(shot_path, nes.ppu.fb) ? "screenshot: %s" : "screenshot failed: %s", shot_path);
            if (hash_out) {
                uint64_t h = 0xcbf29ce484222325ULL;
                for (int k = 0; k < NES_W * NES_H; k++) {
                    h ^= nes.ppu.fb[k];
                    h *= 0x100000001b3ULL;
                }
                fprintf(hash_out, "%ld %016llx\n", frame_no, (unsigned long long)h);
            }
            for (int w = 0; w < nwshots; w++)
                if (wshots[w].frame == frame_no)
                    snprintf(v.window_shot, sizeof v.window_shot, "%s", wshots[w].path);
            frame_no++;
            size_t n = nes_audio_take(&nes, abuf, sizeof abuf / sizeof abuf[0]);
            if (adev && !ff) {
                if (volume != 100)
                    for (size_t k = 0; k < n; k++)
                        abuf[k] = (int16_t)(abuf[k] * volume / 100);
                SDL_QueueAudio(adev, abuf, (uint32_t)(n * sizeof abuf[0]));
            }
            if (max_frames >= 0 && frame_no >= max_frames) {
                running = false;
                break;
            }
            if (budget && SDL_GetPerformanceCounter() > budget)
                break;
        }
        if (adev) {
            /* Keep ~3 frames queued by nudging the resample ratio (max 0.5%). */
            uint32_t q = SDL_GetQueuedAudioSize(adev);
            if (ff)
                SDL_ClearQueuedAudio(adev);
            double err_ratio = ((double)q - target_queue) / (double)target_queue;
            if (err_ratio > 1) err_ratio = 1;
            if (err_ratio < -1) err_ratio = -1;
            nes.apu.cycles_per_sample = base_cps * (1.0 + 0.005 * err_ratio);
        }
        if (to_run)
            video_present(&v, nes.ppu.fb, dm);
        else
            SDL_Delay(1);
        save_flush(&save, &nes, false);
    }

    save_flush(&save, &nes, true);
    if (frame_no)
        logf_("frames: %ld, core %.3f ms/frame (%.0f fps capable)", frame_no,
              1000.0 * (double)emu_ticks / (double)freq / (double)frame_no,
              (double)frame_no * (double)freq / (double)(emu_ticks ? emu_ticks : 1));
    logf_("exit");
    if (hash_out)
        fclose(hash_out);
    trace_free(&trace);
    devmenu_free(dm);
    if (adev)
        SDL_CloseAudioDevice(adev);
    SDL_Quit();
    cart_free(&cart);
    config_free(cfg);
    return 0;
}
