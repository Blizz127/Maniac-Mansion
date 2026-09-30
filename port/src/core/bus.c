/* CPU address space and the master clock. A CPU cycle is split into a
 * start and end phase (NTSC: 12 master clocks, 4 per PPU dot), with the
 * read/write skew used by Mesen2 so PPU register accesses land on the
 * same dot as in the reference emulator. */
#include "nes.h"

enum { MASTER_START = 6, MASTER_END = 6 };

/* PPU runs behind the CPU master clock by this many master clocks. */
int nes_ppu_offset = 1;

void bus_cycle_start(nes_t *nes, bool read)
{
    nes->cpu.cycles++;
    nes->master += read ? (MASTER_START - 1) : (MASTER_START + 1);
    ppu_run_to(nes, nes->master - nes_ppu_offset);
    apu_cycle(nes);
}

void bus_cycle_end(nes_t *nes, bool read)
{
    nes->master += read ? (MASTER_END + 1) : (MASTER_END - 1);
    ppu_run_to(nes, nes->master - nes_ppu_offset);
}

static uint8_t pad_read(nes_t *nes, int port)
{
    uint8_t bit;
    if (nes->pad_strobe) {
        bit = nes->pad_state[port] & 1;
    } else {
        bit = nes->pad_shift[port] & 1;
        nes->pad_shift[port] = (nes->pad_shift[port] >> 1) | 0x80;
    }
    return (nes->open_bus & 0xE0) | bit;
}

uint8_t bus_read(nes_t *nes, uint16_t addr)
{
    uint8_t v;
    if (addr < 0x2000) {
        v = nes->ram[addr & 0x7FF];
    } else if (addr < 0x4000) {
        v = ppu_reg_read(nes, addr);
    } else if (addr < 0x4020) {
        if (addr == 0x4015) {
            /* $4015 does not drive bit 5; it keeps the open-bus value. */
            v = (apu_status_read(nes, false) & ~0x20) | (nes->open_bus & 0x20);
            /* Reading $4015 does not update the external data bus. */
            return v;
        } else if (addr == 0x4016 || addr == 0x4017) {
            v = pad_read(nes, addr - 0x4016);
        } else {
            v = nes->open_bus;
        }
    } else {
        bool driven = false;
        v = mmc1_prg_read(nes, addr, &driven);
        if (!driven)
            v = nes->open_bus;
    }
    nes->open_bus = v;
    if (nes->mem_hook)
        nes->mem_hook(nes, addr, v, false);
    return v;
}

void bus_write(nes_t *nes, uint16_t addr, uint8_t v)
{
    if (nes->mem_hook)
        nes->mem_hook(nes, addr, v, true);
    nes->open_bus = v;
    if (addr < 0x2000) {
        nes->ram[addr & 0x7FF] = v;
    } else if (addr < 0x4000) {
        ppu_reg_write(nes, addr, v);
    } else if (addr < 0x4020) {
        if (addr == 0x4014) {
            nes->oam_dma_page = v;
            nes->oam_dma_pending = true;
        } else if (addr == 0x4016) {
            bool was = nes->pad_strobe;
            nes->pad_strobe = v & 1;
            if (was && !nes->pad_strobe) {
                nes->pad_shift[0] = nes->pad_state[0];
                nes->pad_shift[1] = nes->pad_state[1];
            }
            if (nes->pad_strobe) {
                nes->pad_shift[0] = nes->pad_state[0];
                nes->pad_shift[1] = nes->pad_state[1];
            }
        } else {
            apu_reg_write(nes, addr, v);
        }
    } else {
        mmc1_prg_write(nes, addr, v);
    }
}

uint8_t bus_peek(nes_t *nes, uint16_t addr)
{
    if (addr < 0x2000)
        return nes->ram[addr & 0x7FF];
    if (addr < 0x4000)
        return ppu_reg_peek(nes, addr);
    if (addr < 0x4020)
        return addr == 0x4015 ? apu_status_read(nes, true) : nes->open_bus;
    bool driven = false;
    uint8_t v = mmc1_prg_read(nes, addr, &driven);
    return driven ? v : nes->open_bus;
}
