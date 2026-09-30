/* mm-headless: deterministic, windowless runner for verification.
 *
 *   mm-headless --rom ROM [--input T.mmin] [--frames N] [--hashes OUT]
 *               [--png-dir DIR --shots 10,200,...] [--ram-fill HEX]
 *   mm-headless --rom nestest.nes --allow-unknown --nestest-log nestest.log
 *   mm-headless --rom test.nes --allow-unknown --blargg [--frames N]
 *
 * Frame hash ("mmfb1"): 64-bit FNV-1a over the 256x240 framebuffer, one
 * step per pixel, pixel value = palette index (0-63) | emphasis << 6.
 * The reference-emulator dumpers in port/tools compute the same thing. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/cart.h"
#include "../core/nes.h"
#include "../core/trace.h"
#include "../host/png.h"

extern int nes_ppu_offset;

static uint64_t fb_hash(const uint16_t *fb, uint16_t mask)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < NES_W * NES_H; i++) {
        h ^= fb[i] & mask;
        h *= 0x100000001b3ULL;
    }
    return h;
}

static long watch_lo = -1, watch_hi = -1;
static char watch_log[4096];
static size_t watch_len;

static void watch_hook(nes_t *nes, uint16_t addr, uint8_t v, bool write)
{
    if (addr < watch_lo || addr > watch_hi || watch_len > sizeof watch_log - 16)
        return;
    watch_len += (size_t)snprintf(watch_log + watch_len, sizeof watch_log - watch_len, write ? " W%04X=%02X" : " R%04X=%02X",
                                  addr, v);
}

static int run_nestest(nes_t *nes, const char *log_path)
{
    FILE *f = fopen(log_path, "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", log_path);
        return 2;
    }
    nes->cpu.pc = 0xC000;
    char line[256];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned pc, a, x, y, p, sp;
        uint64_t cyc;
        char *r = strstr(line, "A:");
        char *c = strstr(line, "CYC:");
        if (!r || !c || sscanf(line, "%4x", &pc) != 1 ||
            sscanf(r, "A:%2x X:%2x Y:%2x P:%2x SP:%2x", &a, &x, &y, &p, &sp) != 5 ||
            sscanf(c, "CYC:%" SCNu64, &cyc) != 1)
            continue;
        cpu_t *k = &nes->cpu;
        uint64_t mycyc = k->cycles;
        if (k->pc != pc || k->a != a || k->x != x || k->y != y || k->p != p || k->s != sp || mycyc != cyc) {
            printf("nestest: MISMATCH at line %d\n  expected %s  got      %04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%" PRIu64 "\n",
                   n + 1, line, k->pc, k->a, k->x, k->y, k->p, k->s, mycyc);
            fclose(f);
            return 1;
        }
        cpu_step(nes);
        n++;
    }
    fclose(f);
    uint8_t r2 = nes->ram[2], r3 = nes->ram[3];
    printf("nestest: %d instructions matched the log; result bytes $02=%02X $03=%02X\n", n, r2, r3);
    return (r2 || r3) ? 1 : 0;
}

static int run_blargg(nes_t *nes, int frames)
{
    for (int fr = 0; fr < frames; fr++) {
        nes_run_frame(nes);
        const uint8_t *m = nes->prgram;
        if (m[1] == 0xDE && m[2] == 0xB0 && m[3] == 0x61) {
            if (m[0] == 0x81) {
                /* test asks for a reset after ~100 ms */
                for (int i = 0; i < 6; i++)
                    nes_run_frame(nes);
                nes_reset(nes);
            } else if (m[0] < 0x80) {
                printf("%.*s", (int)strnlen((const char *)m + 4, 8000), (const char *)m + 4);
                printf("blargg: result %u after %d frames\n", m[0], fr + 1);
                return m[0] ? 1 : 0;
            }
        }
    }
    printf("blargg: no result after %d frames (status %02X)\n", frames, nes->prgram[0]);
    return 3;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: mm-headless --rom ROM [--input T.mmin] [--frames N] [--hashes OUT]\n"
            "                   [--png-dir DIR --shots F1,F2,...] [--ram-fill HEX] [--ppu-offset N]\n"
            "                   [--index-only] [--allow-unknown] [--nestest-log LOG | --blargg] [--state]\n");
}

int main(int argc, char **argv)
{
    const char *rom = NULL, *input = NULL, *hashes = NULL, *png_dir = NULL, *shots = NULL, *nestest = NULL;
    const char *ram_dump = NULL, *load_state = NULL, *save_state = NULL;
    long save_state_at = -1;
    bool allow_unknown = false, blargg = false, state = false, index_only = false;
    long frames = -1;
    unsigned ram_fill = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define ARG(name) (!strcmp(a, name) && v && (i++, 1))
        if (ARG("--rom")) rom = v;
        else if (ARG("--input")) input = v;
        else if (ARG("--frames")) frames = strtol(v, NULL, 0);
        else if (ARG("--hashes")) hashes = v;
        else if (ARG("--png-dir")) png_dir = v;
        else if (ARG("--shots")) shots = v;
        else if (ARG("--ram-dump")) ram_dump = v;
        else if (ARG("--load-state")) load_state = v;
        else if (ARG("--save-state")) {
            char *colon;
            save_state_at = strtol(v, &colon, 10);
            save_state = *colon == ':' ? colon + 1 : NULL;
        }
        else if (ARG("--ram-fill")) ram_fill = (unsigned)strtoul(v, NULL, 16);
        else if (ARG("--ppu-offset")) nes_ppu_offset = atoi(v);
        else if (ARG("--nestest-log")) nestest = v;
        else if (!strcmp(a, "--allow-unknown")) allow_unknown = true;
        else if (!strcmp(a, "--blargg")) blargg = true;
        else if (!strcmp(a, "--state")) state = true;
        else if (!strcmp(a, "--index-only")) index_only = true;
        else if (ARG("--watch")) {
            char *e;
            watch_lo = strtol(v, &e, 16);
            watch_hi = *e == '-' ? strtol(e + 1, NULL, 16) : watch_lo;
        }
        else {
            usage();
            return 2;
        }
#undef ARG
    }
    if (!rom) {
        usage();
        return 2;
    }
    char err[512];
    cart_t cart;
    if (!cart_load(&cart, rom, allow_unknown, err, sizeof err)) {
        fprintf(stderr, "error: %s\n", err);
        return 2;
    }
    static nes_t nes;
    nes_init(&nes, cart.prg, cart.prg_size);
    nes_power(&nes, (uint8_t)ram_fill);
    if (cart.chr_size) /* test ROMs with CHR-ROM */
        memcpy(nes.chrram, cart.chr, cart.chr_size < 8192 ? cart.chr_size : 8192);

    uint8_t prg_id[8];
    for (int i = 0; i < 8; i++)
        prg_id[i] = (uint8_t)strtoul((char[3]){cart.prg_sha256[i * 2], cart.prg_sha256[i * 2 + 1], 0}, NULL, 16);
    static uint8_t state_buf[sizeof(nes_t) + 64];
    if (load_state) {
        FILE *sf = fopen(load_state, "rb");
        size_t n = sf ? fread(state_buf, 1, sizeof state_buf, sf) : 0;
        if (sf)
            fclose(sf);
        if (!nes_state_load(&nes, state_buf, n, prg_id)) {
            fprintf(stderr, "error: cannot load state %s (other build or ROM?)\n", load_state);
            return 2;
        }
    }
    if (nestest)
        return run_nestest(&nes, nestest);
    if (blargg)
        return run_blargg(&nes, frames > 0 ? (int)frames : 3600);

    if (watch_lo >= 0)
        nes.mem_hook = watch_hook;
    trace_t tr = {0};
    if (input && !trace_load(&tr, input, err, sizeof err)) {
        fprintf(stderr, "error: %s\n", err);
        return 2;
    }
    if (frames < 0)
        frames = input ? tr.end : 600;

    FILE *hf = NULL;
    if (hashes) {
        hf = !strcmp(hashes, "-") ? stdout : fopen(hashes, "w");
        if (!hf) {
            fprintf(stderr, "error: cannot write %s\n", hashes);
            return 2;
        }
        fprintf(hf, "# mmfb1%s rom=%s frames=%ld\n", index_only ? "i" : "", cart.sha256, frames);
    }
    FILE *rf = ram_dump ? fopen(ram_dump, "wb") : NULL;
    for (long f = 0; f < frames; f++) {
        uint8_t pads[2];
        trace_at(&tr, (uint32_t)f, pads);
        nes_set_pad(&nes, 0, pads[0]);
        nes_set_pad(&nes, 1, pads[1]);
        nes_run_frame(&nes);
        nes_audio_take(&nes, NULL, (size_t)-1);
        uint64_t h = fb_hash(nes.ppu.fb, index_only ? 0x3F : 0x1FF);
        if (rf)
            fwrite(nes.ram, 1, sizeof nes.ram, rf);
        if (f == save_state_at && save_state) {
            nes_state_save(&nes, state_buf, prg_id);
            FILE *sf = fopen(save_state, "wb");
            if (!sf || fwrite(state_buf, 1, nes_state_size(), sf) != nes_state_size())
                fprintf(stderr, "warning: cannot write state %s\n", save_state);
            if (sf)
                fclose(sf);
        }
        if (hf) {
            fprintf(hf, "%ld %016" PRIx64, f, h);
            if (watch_lo >= 0) {
                fprintf(hf, " watch:%s", watch_len ? watch_log : " -");
                watch_len = 0;
                watch_log[0] = 0;
            }
            if (state)
                fprintf(hf, " pc=%04X a=%02X x=%02X y=%02X p=%02X s=%02X cyc=%" PRIu64 " sl=%d dot=%d",
                        nes.cpu.pc, nes.cpu.a, nes.cpu.x, nes.cpu.y, nes.cpu.p, nes.cpu.s,
                        nes.cpu.cycles, nes.ppu.scanline, nes.ppu.dot);
            if (state)
                fprintf(hf, " spr0=%u,%u", nes.ppu.oam[3], nes.ppu.oam[0]);
            fputc('\n', hf);
        }
        if (png_dir && shots) {
            char key[32];
            snprintf(key, sizeof key, ",%ld,", f);
            char list[1024];
            snprintf(list, sizeof list, ",%s,", shots);
            if (strstr(list, key)) {
                char path[1024];
                snprintf(path, sizeof path, "%s/frame%06ld.png", png_dir, f);
                if (!png_write_nes(path, nes.ppu.fb))
                    fprintf(stderr, "warning: cannot write %s\n", path);
            }
        }
    }
    if (hf && hf != stdout)
        fclose(hf);
    if (rf)
        fclose(rf);
    trace_free(&tr);
    cart_free(&cart);
    return 0;
}
