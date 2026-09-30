/* 2C02 PPU, stepped one dot at a time. `dot` and `scanline` hold the dot
 * most recently executed (as in Mesen2), so a CPU register access sees the
 * PPU exactly as far along as the master clock says it is.
 *
 * Background uses the hardware fetch/shift pipeline (so mid-line $2005/
 * $2006 writes and the sprite-0 scroll split land on the right pixel).
 * Sprites are evaluated for the next line at dot 257 from primary OAM. */
#include <string.h>

#include "nes.h"

static inline bool rendering(const ppu_t *p) { return p->mask & 0x18; }

static inline void update_nmi(nes_t *nes)
{
    ppu_t *p = &nes->ppu;
    nes->cpu.nmi_line = (p->status & 0x80) && (p->ctrl & 0x80);
}

/* ---- PPU address space ---- */

static uint8_t vram_read(nes_t *nes, uint16_t a)
{
    a &= 0x3FFF;
    if (a < 0x2000)
        return mmc1_chr_read(nes, a);
    return nes->ppu.ciram[mmc1_nt_addr(nes, a)];
}

static void vram_write(nes_t *nes, uint16_t a, uint8_t v)
{
    a &= 0x3FFF;
    if (a < 0x2000)
        mmc1_chr_write(nes, a, v);
    else if (a < 0x3F00)
        nes->ppu.ciram[mmc1_nt_addr(nes, a)] = v;
    else {
        uint8_t i = a & 0x1F;
        if ((i & 0x13) == 0x10)
            i &= 0x0F;
        nes->ppu.palette[i] = v & 0x3F;
    }
}

static inline uint8_t pal_read(const ppu_t *p, uint8_t i)
{
    i &= 0x1F;
    if ((i & 0x13) == 0x10)
        i &= 0x0F;
    return p->palette[i];
}

void ppu_power(ppu_t *p)
{
    memset(p, 0, sizeof *p);
    /* Power-up palette (as captured from hardware; Mesen2 default). */
    static const uint8_t boot_pal[32] = {
        0x09, 0x01, 0x00, 0x01, 0x00, 0x02, 0x02, 0x0D, 0x08, 0x10, 0x08, 0x24, 0x00, 0x00, 0x04, 0x2C,
        0x09, 0x01, 0x34, 0x03, 0x00, 0x04, 0x00, 0x14, 0x08, 0x3A, 0x00, 0x02, 0x00, 0x20, 0x2C, 0x08,
    };
    memcpy(p->palette, boot_pal, sizeof boot_pal);
    p->scanline = -1;
    p->dot = 0;
}

void ppu_reset(ppu_t *p)
{
    p->ctrl = 0;
    p->mask = 0;
    p->w = false;
    p->t = 0;
    p->fine_x = 0;
    p->read_buffer = 0;
    p->odd_frame = false;
}

/* ---- scroll counter increments (loopy) ---- */

static inline void inc_x(ppu_t *p)
{
    if ((p->v & 0x001F) == 31) {
        p->v &= ~0x001F;
        p->v ^= 0x0400;
    } else {
        p->v++;
    }
}

static inline void inc_y(ppu_t *p)
{
    if ((p->v & 0x7000) != 0x7000) {
        p->v += 0x1000;
    } else {
        p->v &= ~0x7000;
        int y = (p->v & 0x03E0) >> 5;
        if (y == 29) {
            y = 0;
            p->v ^= 0x0800;
        } else if (y == 31) {
            y = 0;
        } else {
            y++;
        }
        p->v = (p->v & ~0x03E0) | (y << 5);
    }
}

static inline void reload_shifters(ppu_t *p)
{
    p->bg_lo = (p->bg_lo & 0xFF00) | p->pt_lo_latch;
    p->bg_hi = (p->bg_hi & 0xFF00) | p->pt_hi_latch;
    p->at_lo = (p->at_lo & 0xFF00) | ((p->at_latch & 1) ? 0xFF : 0x00);
    p->at_hi = (p->at_hi & 0xFF00) | ((p->at_latch & 2) ? 0xFF : 0x00);
}

static void bg_fetch(nes_t *nes)
{
    ppu_t *p = &nes->ppu;
    switch (p->dot & 7) {
    case 1:
        p->nt_latch = vram_read(nes, 0x2000 | (p->v & 0x0FFF));
        break;
    case 3: {
        uint8_t at = vram_read(nes, 0x23C0 | (p->v & 0x0C00) | ((p->v >> 4) & 0x38) | ((p->v >> 2) & 0x07));
        int shift = ((p->v >> 4) & 4) | (p->v & 2);
        p->at_latch = (at >> shift) & 3;
    } break;
    case 5:
        p->pt_lo_latch = vram_read(nes, ((p->ctrl & 0x10) << 8) | (p->nt_latch << 4) | ((p->v >> 12) & 7));
        break;
    case 7:
        p->pt_hi_latch = vram_read(nes, ((p->ctrl & 0x10) << 8) | (p->nt_latch << 4) | ((p->v >> 12) & 7) | 8);
        break;
    case 0:
        inc_x(p);
        break;
    }
}

static void eval_sprites(nes_t *nes)
{
    ppu_t *p = &nes->ppu;
    int h = (p->ctrl & 0x20) ? 16 : 8;
    int line = p->scanline; /* sprites for line+1 use Y compare against line */
    int n = 0;
    p->spr0_on_line = false;
    for (int i = 0; i < 64; i++) {
        int y = p->oam[i * 4];
        int row = line - y;
        if (row < 0 || row >= h)
            continue;
        if (n == 8) {
            p->status |= 0x20;
            break;
        }
        if (i == 0)
            p->spr0_on_line = true;
        uint8_t tile = p->oam[i * 4 + 1];
        uint8_t attr = p->oam[i * 4 + 2];
        if (attr & 0x80)
            row = h - 1 - row;
        uint16_t addr;
        if (h == 16)
            addr = ((tile & 1) << 12) | ((tile & 0xFE) << 4) | ((row & 8) << 1) | (row & 7);
        else
            addr = ((p->ctrl & 0x08) << 9) | (tile << 4) | row;
        uint8_t lo = vram_read(nes, addr), hi = vram_read(nes, addr + 8);
        if (attr & 0x40) { /* horizontal flip */
            lo = (uint8_t)(((lo * 0x0802LU & 0x22110LU) | (lo * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16);
            hi = (uint8_t)(((hi * 0x0802LU & 0x22110LU) | (hi * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16);
        }
        p->spr_lo[n] = lo;
        p->spr_hi[n] = hi;
        p->spr_attr[n] = attr;
        p->spr_x[n] = p->oam[i * 4 + 3];
        n++;
    }
    p->spr_count = n;
}

static void render_pixel(nes_t *nes)
{
    ppu_t *p = &nes->ppu;
    int x = p->dot - 1;
    uint8_t color;
    if (!rendering(p)) {
        /* With rendering off, the backdrop is palette[0] unless v points
         * into palette RAM, in which case that entry is shown. */
        color = ((p->v & 0x3F00) == 0x3F00) ? pal_read(p, p->v & 0x1F) : pal_read(p, 0);
    } else {
        uint8_t bg = 0;
        if ((p->mask & 0x08) && (x >= 8 || (p->mask & 0x02))) {
            int s = 15 - p->fine_x;
            bg = ((p->bg_lo >> s) & 1) | (((p->bg_hi >> s) & 1) << 1);
            if (bg)
                bg |= (((p->at_lo >> s) & 1) | (((p->at_hi >> s) & 1) << 1)) << 2;
        }
        uint8_t sp = 0;
        bool sp_front = false;
        if ((p->mask & 0x10) && (x >= 8 || (p->mask & 0x04))) {
            for (int i = 0; i < p->spr_count; i++) {
                int off = x - p->spr_x[i];
                if (off < 0 || off > 7)
                    continue;
                int b = 7 - off;
                uint8_t px = ((p->spr_lo[i] >> b) & 1) | (((p->spr_hi[i] >> b) & 1) << 1);
                if (!px)
                    continue;
                if (i == 0 && p->spr0_on_line && (bg & 3) && x != 255)
                    p->status |= 0x40;
                sp = 0x10 | ((p->spr_attr[i] & 3) << 2) | px;
                sp_front = !(p->spr_attr[i] & 0x20);
                break;
            }
        }
        uint8_t idx;
        if ((bg & 3) && (sp & 3))
            idx = sp_front ? sp : bg;
        else if (sp & 3)
            idx = sp;
        else if (bg & 3)
            idx = bg;
        else
            idx = 0;
        color = pal_read(p, idx);
    }
    if (p->mask & 0x01)
        color &= 0x30;
    p->fb[p->scanline * NES_W + x] = color | ((p->mask & 0xE0) << 1);
}

static void exec_dot(nes_t *nes)
{
    ppu_t *p = &nes->ppu;

    if (p->dot > 339) {
        p->dot = 0;
        if (++p->scanline > 260) {
            p->scanline = -1;
            p->spr_count = 0;
        }
        if (p->scanline == 240) {
            p->frame++;
            p->frame_ready = true;
        }
    } else {
        p->dot++;
        int sl = p->scanline;
        bool ren = p->rendering_prev;
        if (sl < 240) {
            if (sl == -1 && p->dot == 1) {
                p->status &= ~0xE0;
                update_nmi(nes);
            }
            int d = p->dot;
            if (ren && ((d >= 2 && d <= 257) || (d >= 322 && d <= 337))) {
                p->bg_lo <<= 1;
                p->bg_hi <<= 1;
                p->at_lo <<= 1;
                p->at_hi <<= 1;
            }
            if (sl >= 0 && d <= 256)
                render_pixel(nes);
            if (ren) {
                if ((d >= 1 && d <= 256) || (d >= 321 && d <= 336)) {
                    if ((d & 7) == 1 && d != 1 && d != 321)
                        reload_shifters(p);
                    bg_fetch(nes);
                }
                if (d == 257 || d == 337)
                    reload_shifters(p);
                if (d == 256)
                    inc_y(p);
                if (d == 257) {
                    p->v = (p->v & ~0x041F) | (p->t & 0x041F);
                    if (sl >= 0)
                        eval_sprites(nes);
                    else
                        p->spr_count = 0;
                }
                if (d >= 257 && d <= 320)
                    p->oam_addr = 0;
                if (sl == -1 && d >= 280 && d <= 304)
                    p->v = (p->v & ~0x7BE0) | (p->t & 0x7BE0);
                if (d == 338 || d == 340)
                    p->nt_latch = vram_read(nes, 0x2000 | (p->v & 0x0FFF));
                if (sl == -1 && d == 339 && p->odd_frame)
                    p->dot = 340;
            } else if (p->dot == 257) {
                p->spr_count = 0;
            }
            if (sl == -1 && p->dot == 340)
                p->odd_frame = !p->odd_frame;
        } else if (sl == 241 && p->dot == 1) {
            if (!p->vbl_suppress)
                p->status |= 0x80;
            p->vbl_suppress = false;
            update_nmi(nes);
        }
    }
    p->rendering_prev = rendering(p);
}

void ppu_run_to(nes_t *nes, uint64_t to)
{
    ppu_t *p = &nes->ppu;
    do {
        exec_dot(nes);
        p->master += 4;
    } while (p->master + 4 <= to);
}

/* ---- CPU-facing registers ---- */

uint8_t ppu_reg_read(nes_t *nes, uint16_t addr)
{
    ppu_t *p = &nes->ppu;
    uint8_t v;
    switch (addr & 7) {
    case 2:
        v = (p->status & 0xE0) | (p->io_latch & 0x1F);
        p->status &= ~0x80;
        p->w = false;
        if (p->scanline == 241 && p->dot < 3) {
            nes->cpu.nmi_line = false;
            nes->cpu.need_nmi = false;
            if (p->dot == 0) {
                p->vbl_suppress = true;
                v &= 0x7F;
            }
        }
        update_nmi(nes);
        p->io_latch = (p->io_latch & 0x1F) | (v & 0xE0);
        return v;
    case 4:
        v = p->oam[p->oam_addr];
        if ((p->oam_addr & 3) == 2)
            v &= 0xE3;
        p->io_latch = v;
        return v;
    case 7: {
        uint16_t a = p->v & 0x3FFF;
        if (a >= 0x3F00) {
            v = (pal_read(p, a & 0x1F) & 0x3F) | (p->io_latch & 0xC0);
            p->read_buffer = vram_read(nes, a - 0x1000);
        } else {
            v = p->read_buffer;
            p->read_buffer = vram_read(nes, a);
        }
        if (rendering(p) && (p->scanline < 240)) {
            inc_x(p);
            inc_y(p);
        } else {
            p->v = (p->v + ((p->ctrl & 0x04) ? 32 : 1)) & 0x7FFF;
        }
        p->io_latch = v;
        return v;
    }
    default:
        return p->io_latch;
    }
}

uint8_t ppu_reg_peek(nes_t *nes, uint16_t addr)
{
    ppu_t *p = &nes->ppu;
    switch (addr & 7) {
    case 2: return (p->status & 0xE0) | (p->io_latch & 0x1F);
    case 4: return p->oam[p->oam_addr];
    case 7: return p->read_buffer;
    default: return p->io_latch;
    }
}

void ppu_reg_write(nes_t *nes, uint16_t addr, uint8_t v)
{
    ppu_t *p = &nes->ppu;
    p->io_latch = v;
    switch (addr & 7) {
    case 0:
        p->ctrl = v;
        p->t = (p->t & ~0x0C00) | ((v & 3) << 10);
        update_nmi(nes);
        break;
    case 1:
        p->mask = v;
        break;
    case 3:
        p->oam_addr = v;
        break;
    case 4:
        if (rendering(p) && p->scanline < 240) {
            p->oam_addr += 4; /* glitchy increment, no write */
        } else {
            p->oam[p->oam_addr++] = v;
        }
        break;
    case 5:
        if (!p->w) {
            p->t = (p->t & ~0x001F) | (v >> 3);
            p->fine_x = v & 7;
        } else {
            p->t = (p->t & ~0x73E0) | ((v & 7) << 12) | ((v & 0xF8) << 2);
        }
        p->w = !p->w;
        break;
    case 6:
        if (!p->w) {
            p->t = (p->t & 0x00FF) | ((v & 0x3F) << 8);
        } else {
            p->t = (p->t & 0x7F00) | v;
            p->v = p->t;
        }
        p->w = !p->w;
        break;
    case 7:
        vram_write(nes, p->v, v);
        if (rendering(p) && p->scanline < 240) {
            inc_x(p);
            inc_y(p);
        } else {
            p->v = (p->v + ((p->ctrl & 0x04) ? 32 : 1)) & 0x7FFF;
        }
        break;
    }
}
