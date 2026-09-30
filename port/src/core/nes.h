/* Maniac Mansion (NES) port: Stage 1 hardware core.
 *
 * Deterministic, host-independent NES core: 2A03 CPU + APU, 2C02 PPU,
 * MMC1 (SNROM: 8 KiB CHR-RAM, 8 KiB battery PRG-RAM) and standard pads.
 * Timing follows a master-clock model (12 master clocks per CPU cycle,
 * 4 per PPU dot, NTSC) so that CPU accesses interleave with PPU dots
 * the way the reference emulator (Mesen2) interleaves them.
 */
#ifndef MM_NES_H
#define MM_NES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { NES_W = 256, NES_H = 240 };

enum {
    PAD_A = 0x01, PAD_B = 0x02, PAD_SELECT = 0x04, PAD_START = 0x08,
    PAD_UP = 0x10, PAD_DOWN = 0x20, PAD_LEFT = 0x40, PAD_RIGHT = 0x80,
};

enum { IRQ_FRAME = 1, IRQ_DMC = 2 };

typedef struct {
    uint16_t pc;
    uint8_t a, x, y, s, p;
    uint64_t cycles;
    bool nmi_line, prev_nmi_line;
    bool need_nmi, prev_need_nmi;
    bool run_irq, prev_run_irq;
    uint8_t irq_sources;
    bool jammed;
} cpu_t;

typedef struct {
    uint8_t ctrl, mask, status;
    uint8_t oam_addr;
    uint16_t v, t;
    uint8_t fine_x;
    bool w;
    uint8_t read_buffer;
    uint8_t io_latch;
    uint8_t oam[256];
    uint8_t palette[32];
    uint8_t ciram[2048];

    int scanline; /* -1 (pre-render) .. 260 */
    int dot;      /* 0 .. 340 */
    bool odd_frame;
    uint64_t frame;
    uint64_t master; /* master clock the PPU has been run up to */

    /* background pipeline */
    uint16_t bg_lo, bg_hi, at_lo, at_hi;
    uint8_t nt_latch, at_latch, pt_lo_latch, pt_hi_latch;

    /* sprites for the line being drawn, pre-resolved per pixel at
     * evaluation: bits 0-4 palette index (0x10-0x1F, 0 = transparent),
     * bit 6 behind background, bit 7 pixel belongs to sprite 0 */
    int spr_count;
    uint8_t spr_line[NES_W];
    bool spr0_on_line;
    /* next line (evaluated during the current line) */
    int nspr_count;
    uint8_t nspr_idx[8];
    bool nspr0;

    bool vbl_suppress; /* $2002 read raced vblank set */
    bool rendering_prev;
    uint16_t fb[NES_W * NES_H]; /* bits 0-5 colour, bits 6-8 emphasis */
    bool frame_ready;
} ppu_t;

typedef struct {
    bool enabled;
    uint8_t duty, duty_pos;
    bool halt, const_vol;
    uint8_t vol;
    uint8_t env_div, env_decay;
    bool env_start;
    uint8_t length;
    uint16_t timer, timer_ctr;
    bool sweep_en, sweep_neg, sweep_reload;
    uint8_t sweep_period, sweep_shift, sweep_div;
    bool second;
} apu_pulse_t;

typedef struct {
    bool enabled, halt, linear_reload;
    uint8_t linear_load, linear_ctr, length, seq;
    uint16_t timer, timer_ctr;
} apu_tri_t;

typedef struct {
    bool enabled, halt, const_vol, mode;
    uint8_t vol, env_div, env_decay, length;
    bool env_start;
    uint16_t period, timer_ctr, lfsr;
} apu_noise_t;

typedef struct {
    bool irq_en, loop;
    uint16_t period, timer_ctr;
    uint8_t output;
    uint16_t sample_addr, sample_len;
    uint16_t cur_addr, bytes_left;
    uint8_t shift, bits_left, buffer;
    bool buffer_full, silence;
    int dma_delay; /* pending DMA request */
} apu_dmc_t;

typedef struct {
    apu_pulse_t pulse[2];
    apu_tri_t tri;
    apu_noise_t noise;
    apu_dmc_t dmc;
    bool mode5, irq_inhibit, frame_irq;
    int32_t frame_ctr;      /* CPU cycles into the frame sequence */
    int frame_write_delay;  /* pending $4017 write, cycles to apply */
    uint8_t frame_write_val;
    /* audio output */
    double sample_acc;
    uint32_t sample_n;
    double cycles_per_sample, sample_phase;
    float hp_prev_in, hp_prev_out, lp_prev;
    int16_t *out;
    size_t out_len, out_cap;
    float mix;       /* current DAC output */
    bool mix_dirty;  /* recompute mix before the next sample */
} apu_t;

typedef struct {
    uint8_t shift, shift_n;
    uint8_t ctrl, chr0, chr1, prg;
    uint64_t last_write_cycle;
    bool mmc1a; /* false: MMC1B (PRG-RAM enable in $E000 bit 4) */
    const uint8_t *prg_lo, *prg_hi; /* banks mapped at $8000 and $C000 */
} mmc1_t;

typedef struct nes {
    cpu_t cpu;
    ppu_t ppu;
    apu_t apu;
    mmc1_t mmc1;

    uint8_t ram[2048];
    uint8_t prgram[8192];
    uint8_t chrram[8192];
    const uint8_t *prg;
    size_t prg_size;
    bool prgram_dirty;

    uint64_t master; /* master clock (12 per CPU cycle) */
    uint8_t open_bus;
    uint8_t pad_state[2], pad_shift[2];
    bool pad_strobe;

    /* OAM DMA */
    bool oam_dma_pending;
    uint8_t oam_dma_page;

    /* instrumentation for trace/diagnostics */
    void (*exec_hook)(struct nes *, void *);
    void *exec_hook_ud;
    void (*mem_hook)(struct nes *, uint16_t addr, uint8_t v, bool write);
} nes_t;

/* Core API (nes.c) */
void nes_init(nes_t *nes, const uint8_t *prg, size_t prg_size);
void nes_power(nes_t *nes, uint8_t ram_fill);
void nes_reset(nes_t *nes);
void nes_run_frame(nes_t *nes);
void nes_set_pad(nes_t *nes, int port, uint8_t buttons);
void nes_audio_config(nes_t *nes, int sample_rate);
/* Drains generated audio; returns the number of mono s16 samples copied. */
size_t nes_audio_take(nes_t *nes, int16_t *dst, size_t max);

/* Snapshots (host-side quick save; never the game's own save format).
 * A state is only valid for the same build and ROM: it records a layout
 * signature and the PRG SHA-256 prefix and is refused otherwise. */
size_t nes_state_size(void);
void nes_state_save(const nes_t *nes, void *buf, const uint8_t prg_id[8]);
bool nes_state_load(nes_t *nes, const void *buf, size_t len, const uint8_t prg_id[8]);

/* Bus (bus.c) */
uint8_t bus_read(nes_t *nes, uint16_t addr);
void bus_write(nes_t *nes, uint16_t addr, uint8_t v);
uint8_t bus_peek(nes_t *nes, uint16_t addr); /* no side effects */
void bus_cycle_start(nes_t *nes, bool read);
void bus_cycle_end(nes_t *nes, bool read);

/* CPU (cpu.c) */
void cpu_power(nes_t *nes);
void cpu_reset(nes_t *nes);
void cpu_step(nes_t *nes);
uint8_t cpu_read(nes_t *nes, uint16_t addr);
void cpu_write(nes_t *nes, uint16_t addr, uint8_t v);

/* PPU (ppu.c) */
void ppu_power(ppu_t *ppu);
void ppu_reset(ppu_t *ppu);
void ppu_run_to(nes_t *nes, uint64_t master);
uint8_t ppu_reg_read(nes_t *nes, uint16_t addr);
void ppu_reg_write(nes_t *nes, uint16_t addr, uint8_t v);
uint8_t ppu_reg_peek(nes_t *nes, uint16_t addr);

/* APU (apu.c) */
void apu_power(nes_t *nes);
void apu_reset(nes_t *nes);
void apu_cycle(nes_t *nes);
uint8_t apu_status_read(nes_t *nes, bool peek);
void apu_reg_write(nes_t *nes, uint16_t addr, uint8_t v);

/* MMC1 (mmc1.c) */
void mmc1_power(nes_t *nes);
uint8_t mmc1_prg_read(nes_t *nes, uint16_t addr, bool *driven);
void mmc1_prg_write(nes_t *nes, uint16_t addr, uint8_t v);
uint8_t mmc1_chr_read(nes_t *nes, uint16_t addr);
void mmc1_chr_write(nes_t *nes, uint16_t addr, uint8_t v);
uint16_t mmc1_nt_addr(nes_t *nes, uint16_t addr);
int mmc1_prg_bank_at(nes_t *nes, uint16_t addr);
void mmc1_refresh(nes_t *nes); /* recompute bank pointers (after a state load) */

#endif
