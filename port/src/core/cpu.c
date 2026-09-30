/* 2A03 CPU core. Every bus cycle is a real read or write (including the
 * 6502's dummy accesses), so memory-mapped side effects, PPU dot timing and
 * mapper write filtering behave as on hardware. Interrupts are polled at
 * the end of the penultimate cycle, via the prev_* latches. */
#include "nes.h"

enum { FC = 0x01, FZ = 0x02, FI = 0x04, FD = 0x08, FB = 0x10, FU = 0x20, FV = 0x40, FN = 0x80 };

static void end_cycle_poll(nes_t *nes)
{
    cpu_t *c = &nes->cpu;
    c->prev_need_nmi = c->need_nmi;
    if (!c->prev_nmi_line && c->nmi_line)
        c->need_nmi = true;
    c->prev_nmi_line = c->nmi_line;
    c->prev_run_irq = c->run_irq;
    c->run_irq = c->irq_sources && !(c->p & FI);
}

static void oam_dma(nes_t *nes);

uint8_t cpu_read(nes_t *nes, uint16_t addr)
{
    if (nes->oam_dma_pending)
        oam_dma(nes);
    bus_cycle_start(nes, true);
    uint8_t v = bus_read(nes, addr);
    bus_cycle_end(nes, true);
    end_cycle_poll(nes);
    return v;
}

void cpu_write(nes_t *nes, uint16_t addr, uint8_t v)
{
    bus_cycle_start(nes, false);
    bus_write(nes, addr, v);
    bus_cycle_end(nes, false);
    end_cycle_poll(nes);
}

/* A DMA halt/alignment cycle: the CPU repeats its read. */
static void dma_idle_read(nes_t *nes, uint16_t addr)
{
    bus_cycle_start(nes, true);
    bus_read(nes, addr);
    bus_cycle_end(nes, true);
    end_cycle_poll(nes);
}

static void oam_dma(nes_t *nes)
{
    /* Triggered on the CPU's next read cycle after the $4014 write. */
    nes->oam_dma_pending = false;
    uint16_t base = (uint16_t)nes->oam_dma_page << 8;
    uint16_t halt_addr = nes->cpu.pc;
    dma_idle_read(nes, halt_addr); /* halt cycle */
    if (nes->cpu.cycles & 1)      /* align to a get cycle */
        dma_idle_read(nes, halt_addr);
    for (int i = 0; i < 256; i++) {
        bus_cycle_start(nes, true);
        uint8_t v = bus_read(nes, base + i);
        bus_cycle_end(nes, true);
        end_cycle_poll(nes);
        bus_cycle_start(nes, false);
        bus_write(nes, 0x2004, v);
        bus_cycle_end(nes, false);
        end_cycle_poll(nes);
    }
}

#define RD(a) cpu_read(nes, (uint16_t)(a))
#define WR(a, v) cpu_write(nes, (uint16_t)(a), (uint8_t)(v))

static inline void setzn(cpu_t *c, uint8_t v)
{
    c->p = (c->p & ~(FZ | FN)) | (v ? 0 : FZ) | (v & FN);
}

static inline void push(nes_t *nes, uint8_t v)
{
    WR(0x100 | nes->cpu.s, v);
    nes->cpu.s--;
}

static inline uint8_t pull(nes_t *nes)
{
    nes->cpu.s++;
    return RD(0x100 | nes->cpu.s);
}

static void interrupt(nes_t *nes, bool brk)
{
    cpu_t *c = &nes->cpu;
    if (brk) {
        RD(c->pc++); /* padding byte */
    } else {
        RD(c->pc);
        RD(c->pc);
    }
    push(nes, c->pc >> 8);
    push(nes, c->pc & 0xFF);
    uint16_t vec;
    if (c->need_nmi) { /* NMI can hijack BRK/IRQ */
        c->need_nmi = false;
        vec = 0xFFFA;
    } else {
        vec = 0xFFFE;
    }
    push(nes, c->p | FU | (brk ? FB : 0));
    c->p |= FI;
    uint8_t lo = RD(vec);
    uint8_t hi = RD(vec + 1);
    c->pc = lo | (hi << 8);
    /* An NMI raised during the vector fetch waits until the handler's
     * first instruction has run. */
    if (brk)
        c->prev_need_nmi = false;
}

void cpu_power(nes_t *nes)
{
    cpu_t *c = &nes->cpu;
    c->a = c->x = c->y = 0;
    c->s = 0xFD;
    c->p = FI | FU;
    c->irq_sources = 0;
    c->nmi_line = c->prev_nmi_line = false;
    c->need_nmi = c->prev_need_nmi = false;
    c->run_irq = c->prev_run_irq = false;
    c->jammed = false;
    c->cycles = (uint64_t)-1;
    c->pc = bus_peek(nes, 0xFFFC) | (bus_peek(nes, 0xFFFD) << 8);
    /* The CPU takes 8 cycles before it starts executing after power-up. */
    for (int i = 0; i < 8; i++) {
        bus_cycle_start(nes, true);
        bus_cycle_end(nes, true);
    }
}

void cpu_reset(nes_t *nes)
{
    cpu_t *c = &nes->cpu;
    c->s -= 3;
    c->p |= FI;
    c->jammed = false;
    c->need_nmi = c->prev_need_nmi = false;
    c->pc = bus_peek(nes, 0xFFFC) | (bus_peek(nes, 0xFFFD) << 8);
    for (int i = 0; i < 8; i++) {
        bus_cycle_start(nes, true);
        bus_cycle_end(nes, true);
    }
}

/* ---- addressing: each returns the effective address after its cycles ---- */

static inline uint16_t am_zp(nes_t *nes) { return RD(nes->cpu.pc++); }

static inline uint16_t am_zpi(nes_t *nes, uint8_t idx)
{
    uint8_t b = RD(nes->cpu.pc++);
    RD(b);
    return (uint8_t)(b + idx);
}

static inline uint16_t am_abs(nes_t *nes)
{
    uint8_t lo = RD(nes->cpu.pc++);
    uint8_t hi = RD(nes->cpu.pc++);
    return lo | (hi << 8);
}

/* always_dummy: writes and RMW always take the fix-up cycle */
static inline uint16_t am_absi(nes_t *nes, uint8_t idx, bool always_dummy)
{
    uint8_t lo = RD(nes->cpu.pc++);
    uint8_t hi = RD(nes->cpu.pc++);
    uint16_t base = lo | (hi << 8);
    uint16_t ea = base + idx;
    if (always_dummy || ((base ^ ea) & 0xFF00))
        RD((base & 0xFF00) | (ea & 0xFF));
    return ea;
}

static inline uint16_t am_izx(nes_t *nes)
{
    uint8_t p = RD(nes->cpu.pc++);
    RD(p);
    p += nes->cpu.x;
    uint8_t lo = RD(p);
    uint8_t hi = RD((uint8_t)(p + 1));
    return lo | (hi << 8);
}

static inline uint16_t am_izy(nes_t *nes, bool always_dummy)
{
    uint8_t p = RD(nes->cpu.pc++);
    uint8_t lo = RD(p);
    uint8_t hi = RD((uint8_t)(p + 1));
    uint16_t base = lo | (hi << 8);
    uint16_t ea = base + nes->cpu.y;
    if (always_dummy || ((base ^ ea) & 0xFF00))
        RD((base & 0xFF00) | (ea & 0xFF));
    return ea;
}

/* ---- ALU ---- */

static inline void op_adc(cpu_t *c, uint8_t m)
{
    unsigned r = c->a + m + (c->p & FC);
    c->p &= ~(FC | FV);
    if (r > 0xFF) c->p |= FC;
    if (~(c->a ^ m) & (c->a ^ r) & 0x80) c->p |= FV;
    c->a = (uint8_t)r;
    setzn(c, c->a);
}

static inline void op_sbc(cpu_t *c, uint8_t m) { op_adc(c, m ^ 0xFF); }

static inline void op_cmp(cpu_t *c, uint8_t r, uint8_t m)
{
    c->p = (c->p & ~FC) | (r >= m ? FC : 0);
    setzn(c, (uint8_t)(r - m));
}

static inline uint8_t op_asl(cpu_t *c, uint8_t v)
{
    c->p = (c->p & ~FC) | (v >> 7);
    v <<= 1;
    setzn(c, v);
    return v;
}

static inline uint8_t op_lsr(cpu_t *c, uint8_t v)
{
    c->p = (c->p & ~FC) | (v & 1);
    v >>= 1;
    setzn(c, v);
    return v;
}

static inline uint8_t op_rol(cpu_t *c, uint8_t v)
{
    uint8_t cin = c->p & FC;
    c->p = (c->p & ~FC) | (v >> 7);
    v = (uint8_t)(v << 1) | cin;
    setzn(c, v);
    return v;
}

static inline uint8_t op_ror(cpu_t *c, uint8_t v)
{
    uint8_t cin = (c->p & FC) << 7;
    c->p = (c->p & ~FC) | (v & 1);
    v = (v >> 1) | cin;
    setzn(c, v);
    return v;
}

static inline void op_bit(cpu_t *c, uint8_t m)
{
    c->p = (c->p & ~(FZ | FV | FN)) | ((c->a & m) ? 0 : FZ) | (m & (FV | FN));
}

static void branch(nes_t *nes, bool cond)
{
    cpu_t *c = &nes->cpu;
    int8_t off = (int8_t)RD(c->pc++);
    if (!cond)
        return;
    /* A taken branch without a page crossing delays a newly raised IRQ. */
    if (c->run_irq && !c->prev_run_irq)
        c->run_irq = false;
    RD(c->pc);
    uint16_t dst = c->pc + off;
    if ((dst ^ c->pc) & 0xFF00)
        RD((c->pc & 0xFF00) | (dst & 0xFF));
    c->pc = dst;
}

/* RMW helper: read, dummy write of the old value, write the new value. */
#define RMW(addr_expr, fn)                    \
    do {                                      \
        uint16_t ea_ = (addr_expr);           \
        uint8_t v_ = RD(ea_);                 \
        WR(ea_, v_);                          \
        v_ = fn;                              \
        WR(ea_, v_);                          \
        m = v_;                               \
    } while (0)

void cpu_step(nes_t *nes)
{
    cpu_t *c = &nes->cpu;
    if (c->jammed) {
        RD(c->pc);
        return;
    }
    if (nes->exec_hook)
        nes->exec_hook(nes, nes->exec_hook_ud);

    uint8_t op = RD(c->pc++);
    uint8_t m = 0;
    uint16_t ea;
    (void)m;

    switch (op) {
    /* ---- loads ---- */
    case 0xA9: c->a = RD(c->pc++); setzn(c, c->a); break;
    case 0xA5: c->a = RD(am_zp(nes)); setzn(c, c->a); break;
    case 0xB5: c->a = RD(am_zpi(nes, c->x)); setzn(c, c->a); break;
    case 0xAD: c->a = RD(am_abs(nes)); setzn(c, c->a); break;
    case 0xBD: c->a = RD(am_absi(nes, c->x, false)); setzn(c, c->a); break;
    case 0xB9: c->a = RD(am_absi(nes, c->y, false)); setzn(c, c->a); break;
    case 0xA1: c->a = RD(am_izx(nes)); setzn(c, c->a); break;
    case 0xB1: c->a = RD(am_izy(nes, false)); setzn(c, c->a); break;

    case 0xA2: c->x = RD(c->pc++); setzn(c, c->x); break;
    case 0xA6: c->x = RD(am_zp(nes)); setzn(c, c->x); break;
    case 0xB6: c->x = RD(am_zpi(nes, c->y)); setzn(c, c->x); break;
    case 0xAE: c->x = RD(am_abs(nes)); setzn(c, c->x); break;
    case 0xBE: c->x = RD(am_absi(nes, c->y, false)); setzn(c, c->x); break;

    case 0xA0: c->y = RD(c->pc++); setzn(c, c->y); break;
    case 0xA4: c->y = RD(am_zp(nes)); setzn(c, c->y); break;
    case 0xB4: c->y = RD(am_zpi(nes, c->x)); setzn(c, c->y); break;
    case 0xAC: c->y = RD(am_abs(nes)); setzn(c, c->y); break;
    case 0xBC: c->y = RD(am_absi(nes, c->x, false)); setzn(c, c->y); break;

    /* ---- stores ---- */
    case 0x85: WR(am_zp(nes), c->a); break;
    case 0x95: WR(am_zpi(nes, c->x), c->a); break;
    case 0x8D: WR(am_abs(nes), c->a); break;
    case 0x9D: WR(am_absi(nes, c->x, true), c->a); break;
    case 0x99: WR(am_absi(nes, c->y, true), c->a); break;
    case 0x81: WR(am_izx(nes), c->a); break;
    case 0x91: WR(am_izy(nes, true), c->a); break;
    case 0x86: WR(am_zp(nes), c->x); break;
    case 0x96: WR(am_zpi(nes, c->y), c->x); break;
    case 0x8E: WR(am_abs(nes), c->x); break;
    case 0x84: WR(am_zp(nes), c->y); break;
    case 0x94: WR(am_zpi(nes, c->x), c->y); break;
    case 0x8C: WR(am_abs(nes), c->y); break;

    /* ---- transfers ---- */
    case 0xAA: RD(c->pc); c->x = c->a; setzn(c, c->x); break;
    case 0xA8: RD(c->pc); c->y = c->a; setzn(c, c->y); break;
    case 0x8A: RD(c->pc); c->a = c->x; setzn(c, c->a); break;
    case 0x98: RD(c->pc); c->a = c->y; setzn(c, c->a); break;
    case 0xBA: RD(c->pc); c->x = c->s; setzn(c, c->x); break;
    case 0x9A: RD(c->pc); c->s = c->x; break;

    /* ---- stack ---- */
    case 0x48: RD(c->pc); push(nes, c->a); break;
    case 0x08: RD(c->pc); push(nes, c->p | FB | FU); break;
    case 0x68: RD(c->pc); RD(0x100 | c->s); c->a = pull(nes); setzn(c, c->a); break;
    case 0x28: RD(c->pc); RD(0x100 | c->s); c->p = (pull(nes) & ~FB) | FU; break;

    /* ---- logic / arithmetic ---- */
#define ALU_GROUP(base, stmt)                                              \
    case base + 0x09: { m = RD(c->pc++); stmt; } break;                     \
    case base + 0x05: { m = RD(am_zp(nes)); stmt; } break;                  \
    case base + 0x15: { m = RD(am_zpi(nes, c->x)); stmt; } break;           \
    case base + 0x0D: { m = RD(am_abs(nes)); stmt; } break;                 \
    case base + 0x1D: { m = RD(am_absi(nes, c->x, false)); stmt; } break;   \
    case base + 0x19: { m = RD(am_absi(nes, c->y, false)); stmt; } break;   \
    case base + 0x01: { m = RD(am_izx(nes)); stmt; } break;                 \
    case base + 0x11: { m = RD(am_izy(nes, false)); stmt; } break;
    ALU_GROUP(0x00, c->a |= m; setzn(c, c->a))
    ALU_GROUP(0x20, c->a &= m; setzn(c, c->a))
    ALU_GROUP(0x40, c->a ^= m; setzn(c, c->a))
    ALU_GROUP(0x60, op_adc(c, m))
    ALU_GROUP(0xC0, op_cmp(c, c->a, m))
    ALU_GROUP(0xE0, op_sbc(c, m))
#undef ALU_GROUP
    case 0xEB: m = RD(c->pc++); op_sbc(c, m); break; /* unofficial SBC # */

    case 0xE0: op_cmp(c, c->x, RD(c->pc++)); break;
    case 0xE4: op_cmp(c, c->x, RD(am_zp(nes))); break;
    case 0xEC: op_cmp(c, c->x, RD(am_abs(nes))); break;
    case 0xC0: op_cmp(c, c->y, RD(c->pc++)); break;
    case 0xC4: op_cmp(c, c->y, RD(am_zp(nes))); break;
    case 0xCC: op_cmp(c, c->y, RD(am_abs(nes))); break;

    case 0x24: op_bit(c, RD(am_zp(nes))); break;
    case 0x2C: op_bit(c, RD(am_abs(nes))); break;

    /* ---- inc/dec ---- */
    case 0xE8: RD(c->pc); c->x++; setzn(c, c->x); break;
    case 0xC8: RD(c->pc); c->y++; setzn(c, c->y); break;
    case 0xCA: RD(c->pc); c->x--; setzn(c, c->x); break;
    case 0x88: RD(c->pc); c->y--; setzn(c, c->y); break;

#define RMW_GROUP(base, fn)                                          \
    case base + 0x06: RMW(am_zp(nes), fn); break;                    \
    case base + 0x16: RMW(am_zpi(nes, c->x), fn); break;             \
    case base + 0x0E: RMW(am_abs(nes), fn); break;                   \
    case base + 0x1E: RMW(am_absi(nes, c->x, true), fn); break;
    RMW_GROUP(0x00, op_asl(c, v_))
    RMW_GROUP(0x20, op_rol(c, v_))
    RMW_GROUP(0x40, op_lsr(c, v_))
    RMW_GROUP(0x60, op_ror(c, v_))
    RMW_GROUP(0xC0, (setzn(c, (uint8_t)(v_ - 1)), (uint8_t)(v_ - 1)))
    RMW_GROUP(0xE0, (setzn(c, (uint8_t)(v_ + 1)), (uint8_t)(v_ + 1)))
#undef RMW_GROUP

    case 0x0A: RD(c->pc); c->a = op_asl(c, c->a); break;
    case 0x2A: RD(c->pc); c->a = op_rol(c, c->a); break;
    case 0x4A: RD(c->pc); c->a = op_lsr(c, c->a); break;
    case 0x6A: RD(c->pc); c->a = op_ror(c, c->a); break;

    /* ---- flags ---- */
    case 0x18: RD(c->pc); c->p &= ~FC; break;
    case 0x38: RD(c->pc); c->p |= FC; break;
    case 0x58: RD(c->pc); c->p &= ~FI; break;
    case 0x78: RD(c->pc); c->p |= FI; break;
    case 0xB8: RD(c->pc); c->p &= ~FV; break;
    case 0xD8: RD(c->pc); c->p &= ~FD; break;
    case 0xF8: RD(c->pc); c->p |= FD; break;

    /* ---- branches ---- */
    case 0x10: branch(nes, !(c->p & FN)); break;
    case 0x30: branch(nes, c->p & FN); break;
    case 0x50: branch(nes, !(c->p & FV)); break;
    case 0x70: branch(nes, c->p & FV); break;
    case 0x90: branch(nes, !(c->p & FC)); break;
    case 0xB0: branch(nes, c->p & FC); break;
    case 0xD0: branch(nes, !(c->p & FZ)); break;
    case 0xF0: branch(nes, c->p & FZ); break;

    /* ---- jumps ---- */
    case 0x4C: c->pc = am_abs(nes); break;
    case 0x6C: {
        uint16_t p = am_abs(nes);
        uint8_t lo = RD(p);
        uint8_t hi = RD((p & 0xFF00) | ((p + 1) & 0xFF));
        c->pc = lo | (hi << 8);
    } break;
    case 0x20: {
        uint8_t lo = RD(c->pc++);
        RD(0x100 | c->s);
        push(nes, c->pc >> 8);
        push(nes, c->pc & 0xFF);
        uint8_t hi = RD(c->pc);
        c->pc = lo | (hi << 8);
    } break;
    case 0x60: {
        RD(c->pc);
        RD(0x100 | c->s);
        uint8_t lo = pull(nes);
        uint8_t hi = pull(nes);
        c->pc = lo | (hi << 8);
        RD(c->pc++);
    } break;
    case 0x40: {
        RD(c->pc);
        RD(0x100 | c->s);
        c->p = (pull(nes) & ~FB) | FU;
        uint8_t lo = pull(nes);
        uint8_t hi = pull(nes);
        c->pc = lo | (hi << 8);
    } break;
    case 0x00: interrupt(nes, true); break;

    /* ---- NOPs (official and unofficial) ---- */
    case 0xEA: case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
        RD(c->pc); break;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
        RD(c->pc++); break;
    case 0x04: case 0x44: case 0x64: RD(am_zp(nes)); break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
        RD(am_zpi(nes, c->x)); break;
    case 0x0C: RD(am_abs(nes)); break;
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
        RD(am_absi(nes, c->x, false)); break;

    /* ---- unofficial opcodes ---- */
    case 0xA7: c->a = c->x = RD(am_zp(nes)); setzn(c, c->a); break; /* LAX */
    case 0xB7: c->a = c->x = RD(am_zpi(nes, c->y)); setzn(c, c->a); break;
    case 0xAF: c->a = c->x = RD(am_abs(nes)); setzn(c, c->a); break;
    case 0xBF: c->a = c->x = RD(am_absi(nes, c->y, false)); setzn(c, c->a); break;
    case 0xA3: c->a = c->x = RD(am_izx(nes)); setzn(c, c->a); break;
    case 0xB3: c->a = c->x = RD(am_izy(nes, false)); setzn(c, c->a); break;
    case 0xAB: c->a = c->x = RD(c->pc++); setzn(c, c->a); break; /* LXA (stable-ish) */

    case 0x87: WR(am_zp(nes), c->a & c->x); break; /* SAX */
    case 0x97: WR(am_zpi(nes, c->y), c->a & c->x); break;
    case 0x8F: WR(am_abs(nes), c->a & c->x); break;
    case 0x83: WR(am_izx(nes), c->a & c->x); break;

#define URMW_GROUP(base, fn, post)                                       \
    case base + 0x07: RMW(am_zp(nes), fn); post; break;                  \
    case base + 0x17: RMW(am_zpi(nes, c->x), fn); post; break;           \
    case base + 0x0F: RMW(am_abs(nes), fn); post; break;                 \
    case base + 0x1F: RMW(am_absi(nes, c->x, true), fn); post; break;    \
    case base + 0x1B: RMW(am_absi(nes, c->y, true), fn); post; break;    \
    case base + 0x03: RMW(am_izx(nes), fn); post; break;                 \
    case base + 0x13: RMW(am_izy(nes, true), fn); post; break;
    URMW_GROUP(0x00, op_asl(c, v_), (c->a |= m, setzn(c, c->a)))      /* SLO */
    URMW_GROUP(0x20, op_rol(c, v_), (c->a &= m, setzn(c, c->a)))      /* RLA */
    URMW_GROUP(0x40, op_lsr(c, v_), (c->a ^= m, setzn(c, c->a)))      /* SRE */
    URMW_GROUP(0x60, op_ror(c, v_), op_adc(c, m))                    /* RRA */
    URMW_GROUP(0xC0, (uint8_t)(v_ - 1), op_cmp(c, c->a, m))          /* DCP */
    URMW_GROUP(0xE0, (uint8_t)(v_ + 1), op_sbc(c, m))                /* ISC */
#undef URMW_GROUP

    case 0x0B: case 0x2B: /* ANC */
        c->a &= RD(c->pc++); setzn(c, c->a);
        c->p = (c->p & ~FC) | (c->a >> 7);
        break;
    case 0x4B: /* ALR */
        c->a &= RD(c->pc++); c->a = op_lsr(c, c->a);
        break;
    case 0x6B: { /* ARR */
        c->a &= RD(c->pc++);
        c->a = (c->a >> 1) | ((c->p & FC) << 7);
        setzn(c, c->a);
        c->p &= ~(FC | FV);
        if (c->a & 0x40) c->p |= FC;
        if (((c->a >> 6) ^ (c->a >> 5)) & 1) c->p |= FV;
    } break;
    case 0xCB: { /* AXS */
        uint8_t v = RD(c->pc++);
        uint8_t ax = c->a & c->x;
        c->p = (c->p & ~FC) | (ax >= v ? FC : 0);
        c->x = ax - v;
        setzn(c, c->x);
    } break;
    case 0xBB: /* LAS */
        m = RD(am_absi(nes, c->y, false)) & c->s;
        c->a = c->x = c->s = m; setzn(c, m);
        break;
    case 0x9C: { /* SHY abs,X */
        uint8_t lo = RD(c->pc++), hi = RD(c->pc++);
        uint16_t base = lo | (hi << 8), a = base + c->x;
        RD((base & 0xFF00) | (a & 0xFF));
        uint8_t v = c->y & (hi + 1);
        if ((base ^ a) & 0xFF00) a = (a & 0xFF) | (v << 8);
        WR(a, v);
    } break;
    case 0x9E: { /* SHX abs,Y */
        uint8_t lo = RD(c->pc++), hi = RD(c->pc++);
        uint16_t base = lo | (hi << 8), a = base + c->y;
        RD((base & 0xFF00) | (a & 0xFF));
        uint8_t v = c->x & (hi + 1);
        if ((base ^ a) & 0xFF00) a = (a & 0xFF) | (v << 8);
        WR(a, v);
    } break;
    case 0x9F: { /* SHA abs,Y */
        uint8_t lo = RD(c->pc++), hi = RD(c->pc++);
        uint16_t base = lo | (hi << 8), a = base + c->y;
        RD((base & 0xFF00) | (a & 0xFF));
        WR(a, c->a & c->x & (hi + 1));
    } break;
    case 0x93: { /* SHA (zp),Y */
        uint8_t p = RD(c->pc++);
        uint8_t lo = RD(p), hi = RD((uint8_t)(p + 1));
        uint16_t base = lo | (hi << 8), a = base + c->y;
        RD((base & 0xFF00) | (a & 0xFF));
        WR(a, c->a & c->x & (hi + 1));
    } break;
    case 0x9B: { /* TAS */
        uint8_t lo = RD(c->pc++), hi = RD(c->pc++);
        uint16_t base = lo | (hi << 8), a = base + c->y;
        RD((base & 0xFF00) | (a & 0xFF));
        c->s = c->a & c->x;
        WR(a, c->s & (hi + 1));
    } break;
    case 0x8B: /* XAA */
        c->a = c->x & RD(c->pc++); setzn(c, c->a);
        break;

    default: /* KIL/JAM: 0x02 0x12 0x22 0x32 0x42 0x52 0x62 0x72 0x92 0xB2 0xD2 0xF2 */
        c->jammed = true;
        break;
    }
    (void)ea;

    if (c->prev_run_irq || c->prev_need_nmi)
        interrupt(nes, false);
}
