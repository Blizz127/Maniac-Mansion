/* 2A03 APU (NTSC): two pulses, triangle, noise, DMC, frame sequencer.
 * Clocked once per CPU cycle. Output is mixed with the standard nonlinear
 * DAC approximation, box-filtered down to the host rate, then run through
 * the console's first-order high-pass (90 Hz, 440 Hz) and low-pass
 * (14 kHz) stages. */
#include <stdlib.h>
#include <string.h>

#include "nes.h"

static const uint8_t length_tab[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};
static const uint8_t duty_tab[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1},
};
static const uint8_t tri_seq[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};
static const uint16_t noise_tab[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};
static const uint16_t dmc_tab[16] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54,
};

static float pulse_mix[31], tnd_mix[203];

static void init_tables(void)
{
    static bool done;
    if (done)
        return;
    done = true;
    pulse_mix[0] = 0;
    for (int i = 1; i < 31; i++)
        pulse_mix[i] = 95.52f / (8128.0f / i + 100.0f);
    tnd_mix[0] = 0;
    for (int i = 1; i < 203; i++)
        tnd_mix[i] = 163.67f / (24329.0f / i + 100.0f);
}

void apu_power(nes_t *nes)
{
    apu_t *a = &nes->apu;
    int16_t *out = a->out;
    size_t cap = a->out_cap;
    double cps = a->cycles_per_sample;
    memset(a, 0, sizeof *a);
    a->out = out;
    a->out_cap = cap;
    a->cycles_per_sample = cps ? cps : 1789773.0 / 48000.0;
    a->pulse[1].second = true;
    a->noise.lfsr = 1;
    a->noise.period = noise_tab[0];
    a->dmc.period = dmc_tab[0];
    a->dmc.bits_left = 8;
    a->dmc.silence = true;
    /* Power-up behaves as if $4017 = $00 was written just before reset. */
    a->frame_write_delay = 3;
    a->frame_write_val = 0;
    init_tables();
}

void apu_reset(nes_t *nes)
{
    apu_t *a = &nes->apu;
    for (int i = 0; i < 2; i++) {
        a->pulse[i].enabled = false;
        a->pulse[i].length = 0;
    }
    a->tri.enabled = false;
    a->tri.length = 0;
    a->noise.enabled = false;
    a->noise.length = 0;
    a->dmc.bytes_left = 0;
    a->frame_irq = false;
    nes->cpu.irq_sources &= ~(IRQ_FRAME | IRQ_DMC);
    a->frame_write_delay = 3;
    a->frame_write_val = (a->mode5 ? 0x80 : 0) | (a->irq_inhibit ? 0x40 : 0);
}

/* ---- units ---- */

static void env_clock(bool *start, uint8_t *div, uint8_t *decay, uint8_t vol, bool loop)
{
    if (*start) {
        *start = false;
        *decay = 15;
        *div = vol;
    } else if (*div) {
        (*div)--;
    } else {
        *div = vol;
        if (*decay)
            (*decay)--;
        else if (loop)
            *decay = 15;
    }
}

static uint16_t sweep_target(const apu_pulse_t *p)
{
    uint16_t delta = p->timer >> p->sweep_shift;
    if (p->sweep_neg)
        return p->timer - delta - (p->second ? 0 : 1);
    return p->timer + delta;
}

static bool pulse_muted(const apu_pulse_t *p)
{
    return p->timer < 8 || (!p->sweep_neg && sweep_target(p) > 0x7FF);
}

static void quarter_frame(apu_t *a)
{
    for (int i = 0; i < 2; i++) {
        apu_pulse_t *p = &a->pulse[i];
        env_clock(&p->env_start, &p->env_div, &p->env_decay, p->vol, p->halt);
    }
    env_clock(&a->noise.env_start, &a->noise.env_div, &a->noise.env_decay, a->noise.vol, a->noise.halt);
    apu_tri_t *t = &a->tri;
    if (t->linear_reload)
        t->linear_ctr = t->linear_load;
    else if (t->linear_ctr)
        t->linear_ctr--;
    if (!t->halt)
        t->linear_reload = false;
}

static void half_frame(apu_t *a)
{
    for (int i = 0; i < 2; i++) {
        apu_pulse_t *p = &a->pulse[i];
        if (!p->halt && p->length)
            p->length--;
        if (p->sweep_div == 0 && p->sweep_en && p->sweep_shift && !pulse_muted(p))
            p->timer = sweep_target(p);
        if (p->sweep_div == 0 || p->sweep_reload) {
            p->sweep_div = p->sweep_period;
            p->sweep_reload = false;
        } else {
            p->sweep_div--;
        }
    }
    if (!a->tri.halt && a->tri.length)
        a->tri.length--;
    if (!a->noise.halt && a->noise.length)
        a->noise.length--;
}

static void set_frame_irq(nes_t *nes)
{
    if (!nes->apu.irq_inhibit) {
        nes->apu.frame_irq = true;
        nes->cpu.irq_sources |= IRQ_FRAME;
    }
}

static void frame_sequencer(nes_t *nes)
{
    apu_t *a = &nes->apu;
    if (a->frame_write_delay && --a->frame_write_delay == 0) {
        a->mode5 = a->frame_write_val & 0x80;
        a->frame_ctr = 0;
        if (a->mode5) {
            quarter_frame(a);
            half_frame(a);
        }
        return;
    }
    int32_t c = ++a->frame_ctr;
    if (!a->mode5) {
        switch (c) {
        case 7457: quarter_frame(a); break;
        case 14913: quarter_frame(a); half_frame(a); break;
        case 22371: quarter_frame(a); break;
        case 29828: set_frame_irq(nes); break;
        case 29829: quarter_frame(a); half_frame(a); set_frame_irq(nes); break;
        case 29830: set_frame_irq(nes); a->frame_ctr = 0; break;
        }
    } else {
        switch (c) {
        case 7457: quarter_frame(a); break;
        case 14913: quarter_frame(a); half_frame(a); break;
        case 22371: quarter_frame(a); break;
        case 37281: quarter_frame(a); half_frame(a); break;
        case 37282: a->frame_ctr = 0; break;
        }
    }
}

static void dmc_start(apu_dmc_t *d)
{
    d->cur_addr = d->sample_addr;
    d->bytes_left = d->sample_len;
}

static void dmc_fill(nes_t *nes)
{
    apu_dmc_t *d = &nes->apu.dmc;
    if (d->buffer_full || !d->bytes_left)
        return;
    /* DMA fetch; the CPU stall is approximated as reads of the DMC address
     * by the bus owner (the CPU is halted for these cycles). */
    d->buffer = bus_peek(nes, d->cur_addr);
    d->buffer_full = true;
    d->cur_addr = d->cur_addr == 0xFFFF ? 0x8000 : d->cur_addr + 1;
    if (--d->bytes_left == 0) {
        if (d->loop)
            dmc_start(d);
        else if (d->irq_en)
            nes->cpu.irq_sources |= IRQ_DMC;
    }
    d->dma_delay = 4;
}

static void dmc_clock(nes_t *nes)
{
    apu_dmc_t *d = &nes->apu.dmc;
    if (!d->silence) {
        if (d->shift & 1) {
            if (d->output <= 125)
                d->output += 2;
        } else if (d->output >= 2) {
            d->output -= 2;
        }
    }
    d->shift >>= 1;
    if (--d->bits_left == 0) {
        d->bits_left = 8;
        if (d->buffer_full) {
            d->silence = false;
            d->shift = d->buffer;
            d->buffer_full = false;
            dmc_fill(nes);
        } else {
            d->silence = true;
        }
    }
}

static void output_sample(apu_t *a, float s)
{
    /* high-pass 90 Hz and 440 Hz collapsed into one 90 Hz stage + 440 Hz;
     * low-pass 14 kHz */
    float hp = s - a->hp_prev_in + 0.996f * a->hp_prev_out;
    a->hp_prev_in = s;
    a->hp_prev_out = hp;
    a->lp_prev += (hp - a->lp_prev) * 0.815f;
    float v = a->lp_prev * 1.6f;
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    if (a->out_len == a->out_cap) {
        size_t cap = a->out_cap ? a->out_cap * 2 : 4096;
        int16_t *n = realloc(a->out, cap * sizeof *n);
        if (!n)
            return;
        a->out = n;
        a->out_cap = cap;
    }
    a->out[a->out_len++] = (int16_t)(v * 32767.0f);
}

void apu_cycle(nes_t *nes)
{
    apu_t *a = &nes->apu;
    bool apu_tick = nes->cpu.cycles & 1;

    frame_sequencer(nes);

    /* triangle: every CPU cycle */
    apu_tri_t *t = &a->tri;
    if (t->timer_ctr == 0) {
        t->timer_ctr = t->timer;
        if (t->length && t->linear_ctr && t->timer >= 2)
            t->seq = (t->seq + 1) & 31;
    } else {
        t->timer_ctr--;
    }

    if (apu_tick) {
        for (int i = 0; i < 2; i++) {
            apu_pulse_t *p = &a->pulse[i];
            if (p->timer_ctr == 0) {
                p->timer_ctr = p->timer;
                p->duty_pos = (p->duty_pos - 1) & 7;
            } else {
                p->timer_ctr--;
            }
        }
        apu_noise_t *n = &a->noise;
        if (n->timer_ctr == 0) {
            n->timer_ctr = n->period;
            uint16_t fb = (n->lfsr ^ (n->lfsr >> (n->mode ? 6 : 1))) & 1;
            n->lfsr = (n->lfsr >> 1) | (fb << 14);
        } else {
            n->timer_ctr--;
        }
    }
    apu_dmc_t *d = &a->dmc;
    if (d->timer_ctr == 0) {
        d->timer_ctr = d->period - 1;
        dmc_clock(nes);
    } else {
        d->timer_ctr--;
    }
    if (d->dma_delay)
        d->dma_delay--;

    /* mix */
    int p[2];
    for (int i = 0; i < 2; i++) {
        apu_pulse_t *q = &a->pulse[i];
        int vol = q->const_vol ? q->vol : q->env_decay;
        p[i] = (q->length && !pulse_muted(q) && duty_tab[q->duty][q->duty_pos]) ? vol : 0;
    }
    int tv = tri_seq[t->seq];
    apu_noise_t *n = &a->noise;
    int nv = (n->length && !(n->lfsr & 1)) ? (n->const_vol ? n->vol : n->env_decay) : 0;
    float s = pulse_mix[p[0] + p[1]] + tnd_mix[3 * tv + 2 * nv + d->output];
    a->sample_acc += s;
    a->sample_n++;
    a->sample_phase += 1.0;
    if (a->sample_phase >= a->cycles_per_sample) {
        a->sample_phase -= a->cycles_per_sample;
        output_sample(a, (float)(a->sample_acc / a->sample_n));
        a->sample_acc = 0;
        a->sample_n = 0;
    }
}

uint8_t apu_status_read(nes_t *nes, bool peek)
{
    apu_t *a = &nes->apu;
    uint8_t v = 0;
    if (a->pulse[0].length) v |= 0x01;
    if (a->pulse[1].length) v |= 0x02;
    if (a->tri.length) v |= 0x04;
    if (a->noise.length) v |= 0x08;
    if (a->dmc.bytes_left) v |= 0x10;
    if (a->frame_irq) v |= 0x40;
    if (nes->cpu.irq_sources & IRQ_DMC) v |= 0x80;
    if (!peek) {
        a->frame_irq = false;
        nes->cpu.irq_sources &= ~IRQ_FRAME;
    }
    return v;
}

void apu_reg_write(nes_t *nes, uint16_t addr, uint8_t v)
{
    apu_t *a = &nes->apu;
    switch (addr) {
    case 0x4000: case 0x4004: {
        apu_pulse_t *p = &a->pulse[(addr >> 2) & 1];
        p->duty = v >> 6;
        p->halt = v & 0x20;
        p->const_vol = v & 0x10;
        p->vol = v & 0x0F;
    } break;
    case 0x4001: case 0x4005: {
        apu_pulse_t *p = &a->pulse[(addr >> 2) & 1];
        p->sweep_en = v & 0x80;
        p->sweep_period = (v >> 4) & 7;
        p->sweep_neg = v & 0x08;
        p->sweep_shift = v & 7;
        p->sweep_reload = true;
    } break;
    case 0x4002: case 0x4006: {
        apu_pulse_t *p = &a->pulse[(addr >> 2) & 1];
        p->timer = (p->timer & 0x700) | v;
    } break;
    case 0x4003: case 0x4007: {
        apu_pulse_t *p = &a->pulse[(addr >> 2) & 1];
        p->timer = (p->timer & 0xFF) | ((v & 7) << 8);
        if (p->enabled)
            p->length = length_tab[v >> 3];
        p->duty_pos = 0;
        p->env_start = true;
    } break;
    case 0x4008:
        a->tri.halt = v & 0x80;
        a->tri.linear_load = v & 0x7F;
        break;
    case 0x400A:
        a->tri.timer = (a->tri.timer & 0x700) | v;
        break;
    case 0x400B:
        a->tri.timer = (a->tri.timer & 0xFF) | ((v & 7) << 8);
        if (a->tri.enabled)
            a->tri.length = length_tab[v >> 3];
        a->tri.linear_reload = true;
        break;
    case 0x400C:
        a->noise.halt = v & 0x20;
        a->noise.const_vol = v & 0x10;
        a->noise.vol = v & 0x0F;
        break;
    case 0x400E:
        a->noise.mode = v & 0x80;
        a->noise.period = noise_tab[v & 0x0F];
        break;
    case 0x400F:
        if (a->noise.enabled)
            a->noise.length = length_tab[v >> 3];
        a->noise.env_start = true;
        break;
    case 0x4010:
        a->dmc.irq_en = v & 0x80;
        a->dmc.loop = v & 0x40;
        a->dmc.period = dmc_tab[v & 0x0F];
        if (!a->dmc.irq_en)
            nes->cpu.irq_sources &= ~IRQ_DMC;
        break;
    case 0x4011:
        a->dmc.output = v & 0x7F;
        break;
    case 0x4012:
        a->dmc.sample_addr = 0xC000 | (v << 6);
        break;
    case 0x4013:
        a->dmc.sample_len = (v << 4) | 1;
        break;
    case 0x4015:
        a->pulse[0].enabled = v & 1;
        a->pulse[1].enabled = v & 2;
        a->tri.enabled = v & 4;
        a->noise.enabled = v & 8;
        if (!(v & 1)) a->pulse[0].length = 0;
        if (!(v & 2)) a->pulse[1].length = 0;
        if (!(v & 4)) a->tri.length = 0;
        if (!(v & 8)) a->noise.length = 0;
        nes->cpu.irq_sources &= ~IRQ_DMC;
        if (!(v & 0x10)) {
            a->dmc.bytes_left = 0;
        } else if (!a->dmc.bytes_left) {
            dmc_start(&a->dmc);
            dmc_fill(nes);
        }
        break;
    case 0x4017:
        a->irq_inhibit = v & 0x40;
        if (a->irq_inhibit) {
            a->frame_irq = false;
            nes->cpu.irq_sources &= ~IRQ_FRAME;
        }
        a->frame_write_val = v;
        a->frame_write_delay = (nes->cpu.cycles & 1) ? 4 : 3;
        break;
    }
}
