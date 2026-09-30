#include <stddef.h>
#include <string.h>

#include "nes.h"

void nes_init(nes_t *nes, const uint8_t *prg, size_t prg_size)
{
    int16_t *out = nes->apu.out;
    size_t cap = nes->apu.out_cap;
    double cps = nes->apu.cycles_per_sample;
    memset(nes, 0, sizeof *nes);
    nes->apu.out = out;
    nes->apu.out_cap = cap;
    nes->apu.cycles_per_sample = cps;
    nes->prg = prg;
    nes->prg_size = prg_size;
}

void nes_power(nes_t *nes, uint8_t ram_fill)
{
    memset(nes->ram, ram_fill, sizeof nes->ram);
    memset(nes->chrram, 0, sizeof nes->chrram);
    /* The CPU master clock starts one CPU cycle in (Mesen2: += cpuDivider). */
    nes->master = 12;
    nes->open_bus = 0;
    nes->pad_strobe = false;
    nes->pad_shift[0] = nes->pad_shift[1] = 0;
    nes->oam_dma_pending = false;
    ppu_power(&nes->ppu);
    apu_power(nes);
    mmc1_power(nes);
    cpu_power(nes);
}

void nes_reset(nes_t *nes)
{
    ppu_reset(&nes->ppu);
    apu_reset(nes);
    cpu_reset(nes);
}

void nes_run_frame(nes_t *nes)
{
    nes->ppu.frame_ready = false;
    while (!nes->ppu.frame_ready)
        cpu_step(nes);
}

void nes_set_pad(nes_t *nes, int port, uint8_t buttons)
{
    nes->pad_state[port & 1] = buttons;
    if (nes->pad_strobe)
        nes->pad_shift[port & 1] = buttons;
}

void nes_audio_config(nes_t *nes, int sample_rate)
{
    nes->apu.cycles_per_sample = 1789773.0 / (double)sample_rate;
}

size_t nes_audio_take(nes_t *nes, int16_t *dst, size_t max)
{
    apu_t *a = &nes->apu;
    size_t n = a->out_len < max ? a->out_len : max;
    if (dst)
        memcpy(dst, a->out, n * sizeof *dst);
    memmove(a->out, a->out + n, (a->out_len - n) * sizeof *a->out);
    a->out_len -= n;
    return n;
}

/* ---- snapshots ---- */

typedef struct {
    char magic[8];      /* "MMSTATE1" */
    uint32_t layout;    /* sizeof(nes_t) ^ field offsets that matter */
    uint8_t prg_id[8];
} state_hdr_t;

static uint32_t layout_sig(void)
{
    return (uint32_t)sizeof(nes_t) * 2654435761u ^ (uint32_t)offsetof(nes_t, ppu) ^
           ((uint32_t)offsetof(nes_t, apu) << 8) ^ ((uint32_t)offsetof(nes_t, mmc1) << 16);
}

size_t nes_state_size(void) { return sizeof(state_hdr_t) + sizeof(nes_t); }

void nes_state_save(const nes_t *nes, void *buf, const uint8_t prg_id[8])
{
    state_hdr_t h;
    memcpy(h.magic, "MMSTATE1", 8);
    h.layout = layout_sig();
    memcpy(h.prg_id, prg_id, 8);
    memcpy(buf, &h, sizeof h);
    nes_t *copy = (nes_t *)((uint8_t *)buf + sizeof h);
    memcpy(copy, nes, sizeof *nes);
    /* host pointers are not part of the state */
    copy->prg = NULL;
    copy->apu.out = NULL;
    copy->apu.out_len = copy->apu.out_cap = 0;
    copy->mmc1.prg_lo = copy->mmc1.prg_hi = NULL;
    copy->exec_hook = NULL;
    copy->exec_hook_ud = NULL;
    copy->mem_hook = NULL;
}

bool nes_state_load(nes_t *nes, const void *buf, size_t len, const uint8_t prg_id[8])
{
    state_hdr_t h;
    if (len != nes_state_size())
        return false;
    memcpy(&h, buf, sizeof h);
    if (memcmp(h.magic, "MMSTATE1", 8) || h.layout != layout_sig() || memcmp(h.prg_id, prg_id, 8))
        return false;
    const uint8_t *prg = nes->prg;
    size_t prg_size = nes->prg_size;
    int16_t *out = nes->apu.out;
    size_t out_cap = nes->apu.out_cap;
    double cps = nes->apu.cycles_per_sample;
    void (*eh)(struct nes *, void *) = nes->exec_hook;
    void *eud = nes->exec_hook_ud;
    void (*mh)(struct nes *, uint16_t, uint8_t, bool) = nes->mem_hook;
    memcpy(nes, (const uint8_t *)buf + sizeof h, sizeof *nes);
    nes->prg = prg;
    nes->prg_size = prg_size;
    nes->apu.out = out;
    nes->apu.out_cap = out_cap;
    nes->apu.out_len = 0;
    nes->apu.cycles_per_sample = cps;
    nes->exec_hook = eh;
    nes->exec_hook_ud = eud;
    nes->mem_hook = mh;
    mmc1_refresh(nes);
    return true;
}
