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
    nes->master = 0;
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
