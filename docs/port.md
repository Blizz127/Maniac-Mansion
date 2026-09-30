# Native port (Stage 1)

`port/` is a native Linux x86_64 runtime for the user's own Maniac Mansion
(USA) ROM. It is separate from the matching decompilation: it reads the ROM
at run time, verifies it by SHA-256, and contains no game code or data.

Stage 1 is a deterministic hardware core: a cycle-stepped 2A03 (6502 plus
APU), a dot-based 2C02 PPU, and MMC1 (SNROM: 8 KiB CHR-RAM, 8 KiB battery
PRG-RAM). It is driven by an SDL2 host. Stage 2 will statically recompile the
decomp's reviewed routines to C and co-simulate them against this core.

Defaults are the original game on an NTSC console: 8:7 pixels at an integer
scale, no filters, no enhancements. Anything else is opt-in.

## Build and run

```sh
cmake -S port -B build/port
cmake --build build/port -j
build/port/maniac-mansion-port --rom rom/original.nes   # remembered afterwards
build/port/test-pads                                      # §4a controller tests
```

Needs CMake, a C11 compiler and SDL2 (development package). The release
tarball and the launcher interface are described in
[port-package.md](port-package.md). To build it, run `port/tools/package.sh [TAG]`.

## Verification

All verification output (test ROMs, reference hashes, RAM dumps,
screenshots) stays in the ignored `build/`. Only input traces (`port/traces/*.mmin`)
are committed. They hold button presses and no game data.

### CPU and hardware conformance (M0)

Download the standard test ROMs into `build/port-tests/`
(christopherpow/nes-test-roms), then:

```sh
H=build/port/mm-headless
$H --rom build/port-tests/nestest.nes --allow-unknown --nestest-log build/port-tests/nestest.log
$H --rom build/port-tests/all_instrs.nes --allow-unknown --blargg --frames 4000
```

| Test | Result |
| --- | --- |
| nestest (all 8991 logged instructions: PC, A, X, Y, P, SP, cycle count) | match |
| instr_test-v5 all_instrs / official_only | 16/16, 16/16 |
| instr_misc, instr_timing | 4/4, 2/2 |
| cpu_interrupts_v2 | 5/5 |
| cpu_dummy_writes_oam | pass |
| cpu_dummy_writes_ppumem | official opcodes pass; the unofficial RMW abs,X group fails (known, unused by the game) |
| ppu_vbl_nmi | 10/10 |
| oam_read, apu_test | pass, 8/8 |

### Frame-hash match against the reference emulator (M1)

Hash `mmfb1` is a 64-bit FNV-1a over the 256×240 frame, one step per pixel,
where each pixel is its palette index (0–63) OR'd with emphasis << 6. It is
independent of any RGB palette. The reference is **Mesen2**, which the
dumper configures with a 512-entry "user palette" that encodes the index and
emphasis directly in RGB. That makes the reference hash exact.
**FCEUX 2.6.5** is a cross-check, comparing on index only.

```sh
port/tools/ref-run.sh mesen port/traces/boot.mmin build/port-ref/boot.mesen.txt
build/port/mm-headless --rom rom/original.nes --input port/traces/boot.mmin --hashes build/port-ref/boot.port.txt
python3 port/tools/framehash.py compare build/port-ref/boot.port.txt build/port-ref/boot.mesen.txt
```

| Trace | Frames | Mesen2 | FCEUX (index only, best alignment) |
| --- | --- | --- | --- |
| `boot.mmin`: power-on, title, Lucasfilm logo, intro | 1200 | **1200/1200 identical** | 1185/1185 from frame 13 on (+2 frame alignment) |
| `smoke.mmin`: the input sequence of `tools/trace-smoke.lua` | 900 | **900/900 identical** | 884/884 from frame 16 on |
| START held 176–184 (ad hoc) | 320 | **320/320 identical** | |

FCEUX's first frames differ because its power-up palette RAM contents
differ. Its frame alignment also varies by a couple of frames between
launches: its Qt build runs emulation on its own thread. Mesen2 is the gate.
`mm-headless --ram-dump` and `MM_RAMDUMP` (Mesen) dump the 2 KiB CPU RAM
every frame, so the first diverging frame can be located.

Reference-emulator notes:

- The Mesen2 2.1.1 release binary aborts with `std::bad_cast` on current
  distributions (a known upstream issue). Build it from source, non-AOT, and
  point `MESEN` at it. Its test runner needs `xvfb-run` and a `settings.json`
  (`ref-run.sh` installs `port/tools/mesen-settings.json` into
  `build/port-ref/mesen-home`). That enables Lua I/O and both NES
  controllers.
- In Mesen 2.1.1, `emu.setInput(t, 1)` also overwrites port 0's buttons, so
  the dumper sets port 1 first.
- FCEUX runs from `.tools/fceux` with its bundled libraries and Qt plugins.

## Controllers

DEV_MENU_SPEC §4a is implemented in `port/src/host/pads.c`:

- Before `SDL_Init`, Steam's inherited ignore list is corrected. It un-hides
  28de:1205/1206, 28de:12f0–12ff (Legion Go 2 through InputPlumber) and
  every 17ef:* entry.
- `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` is set.
- The active pad is a physical pad that has produced input. The Steam
  virtual pad is used only when it is the only one producing input, and a
  silent device is never chosen.
- Hotplug is tracked by instance ID. A pad re-added with buttons held must go
  neutral before it counts.
- Logging: `[DEV_MENU] pad: …` once per change, and `[DEV_MENU] input …`
  at most 3 lines per second.

`test-pads` covers the filter, the policy, and an SDL virtual-device case
under the Steam hint environment.
