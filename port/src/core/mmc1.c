/* MMC1 (SxROM). Maniac Mansion USA is SNROM: 256 KiB PRG-ROM, 8 KiB
 * CHR-RAM, 8 KiB battery PRG-RAM at $6000. */
#include "nes.h"

void mmc1_power(nes_t *nes)
{
    mmc1_t *m = &nes->mmc1;
    m->shift = 0;
    m->shift_n = 0;
    m->ctrl = 0x0C; /* PRG mode 3: fix last bank at $C000 */
    m->chr0 = 0;
    m->chr1 = 0;
    m->prg = 0;
    m->last_write_cycle = 0;
    m->mmc1a = false;
}

static unsigned prg_banks16(nes_t *nes) { return (unsigned)(nes->prg_size >> 14); }

int mmc1_prg_bank_at(nes_t *nes, uint16_t addr)
{
    mmc1_t *m = &nes->mmc1;
    unsigned n = prg_banks16(nes);
    /* 512 KiB boards use CHR bit 4 as the outer PRG bank; 256 KiB do not. */
    unsigned outer = n > 16 ? (m->chr0 & 0x10) : 0;
    unsigned bank = m->prg & 0x0F;
    unsigned mode = (m->ctrl >> 2) & 3;
    unsigned b;
    if (mode < 2) {
        b = (bank & 0x0E) | (addr >= 0xC000);
    } else if (mode == 2) {
        b = addr >= 0xC000 ? bank : 0;
    } else {
        b = addr >= 0xC000 ? 0x0F : bank;
    }
    return (int)((b | outer) % n);
}

static bool prgram_enabled(nes_t *nes)
{
    mmc1_t *m = &nes->mmc1;
    /* SNROM's CHR-bit-4 WRAM disable is not modelled (nor by Mesen2);
     * the game only selects CHR-RAM banks 0/1. */
    return m->mmc1a || !(m->prg & 0x10);
}

uint8_t mmc1_prg_read(nes_t *nes, uint16_t addr, bool *driven)
{
    if (addr >= 0x8000) {
        *driven = true;
        int bank = mmc1_prg_bank_at(nes, addr);
        return nes->prg[((size_t)bank << 14) | (addr & 0x3FFF)];
    }
    if (addr >= 0x6000 && prgram_enabled(nes)) {
        *driven = true;
        return nes->prgram[addr & 0x1FFF];
    }
    *driven = false;
    return 0;
}

static void mmc1_reg(nes_t *nes, uint16_t addr, uint8_t v)
{
    mmc1_t *m = &nes->mmc1;
    switch ((addr >> 13) & 3) {
    case 0: m->ctrl = v; break;
    case 1: m->chr0 = v; break;
    case 2: m->chr1 = v; break;
    case 3: m->prg = v; break;
    }
}

void mmc1_prg_write(nes_t *nes, uint16_t addr, uint8_t v)
{
    if (addr < 0x8000) {
        if (addr >= 0x6000 && prgram_enabled(nes)) {
            if (nes->prgram[addr & 0x1FFF] != v)
                nes->prgram_dirty = true;
            nes->prgram[addr & 0x1FFF] = v;
        }
        return;
    }
    mmc1_t *m = &nes->mmc1;
    uint64_t now = nes->cpu.cycles;
    /* The serial port ignores a write on the cycle right after another
     * (the second write of a read-modify-write instruction). */
    bool ignore = now - m->last_write_cycle < 2;
    m->last_write_cycle = now;
    if (ignore)
        return;
    if (v & 0x80) {
        m->shift = 0;
        m->shift_n = 0;
        m->ctrl |= 0x0C;
        return;
    }
    m->shift |= (v & 1) << m->shift_n;
    if (++m->shift_n == 5) {
        mmc1_reg(nes, addr, m->shift);
        m->shift = 0;
        m->shift_n = 0;
    }
}

static size_t chr_offset(nes_t *nes, uint16_t addr)
{
    mmc1_t *m = &nes->mmc1;
    unsigned bank;
    if (m->ctrl & 0x10) /* 4 KiB mode */
        bank = (addr & 0x1000) ? m->chr1 : m->chr0;
    else
        bank = (m->chr0 & 0x1E) | ((addr >> 12) & 1);
    return ((size_t)(bank & 1) << 12) | (addr & 0x0FFF); /* 8 KiB CHR-RAM */
}

uint8_t mmc1_chr_read(nes_t *nes, uint16_t addr)
{
    return nes->chrram[chr_offset(nes, addr)];
}

void mmc1_chr_write(nes_t *nes, uint16_t addr, uint8_t v)
{
    nes->chrram[chr_offset(nes, addr)] = v;
}

uint16_t mmc1_nt_addr(nes_t *nes, uint16_t addr)
{
    addr &= 0x0FFF;
    switch (nes->mmc1.ctrl & 3) {
    case 0: return addr & 0x3FF;                           /* one-screen A */
    case 1: return 0x400 | (addr & 0x3FF);                 /* one-screen B */
    case 2: return (addr & 0x3FF) | ((addr & 0x400));      /* vertical */
    default: return (addr & 0x3FF) | ((addr & 0x800) >> 1); /* horizontal */
    }
}
