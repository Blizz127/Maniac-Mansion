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
| `boot.mmin`: power-on, title, Lucasfilm logo, intro | 1200 | **1200/1200 frames identical** | 1185/1185 from frame 13 on (+2 frame alignment) |
| `smoke.mmin`: the input sequence of `tools/trace-smoke.lua` | 900 | **900/900 frames identical** | 884/884 from frame 16 on |
| START held 176–184 (ad hoc) | 320 | **320/320 frames identical** | |
| `newgame.mmin`: intro, kid select, START, opening dialogue, walk on the verb UI | 8936 | **CPU RAM identical at all 8913 NMI entries; all 80,379 APU register writes identical; nametables, palette, OAM and CHR-RAM identical at every 30th NMI (297 snapshots)** | tracks until the first input, then drifts (frame alignment) |

The power-on state matches Mesen2's reset. The PPU starts at the last dot of
the pre-render line, and the CPU master clock starts one CPU cycle in. As a
result, every NMI lands on the same CPU cycle and PPU dot in both.

On long runs the gameplay trace is gated on machine state, not screen
hashes. On a heavily loaded machine, Mesen2's own screen capture is
nondeterministic: two runs with identical input disagreed on an isolated
frame. The state comparisons are exact and instruction-aligned. The
reference-side switches are `MM_NMIDUMP`, `MM_WRITELOG`, `MM_PPUDUMP` and
`MM_NOSCREEN` for `ref-mesen.lua`, with `--nmi-dump`, `--watch 4000-4017`
and `--ppu-dump` on the port side. `port/tests/gate.sh` runs every check and
writes `build/port-gate.json`, which the release embeds as
`build-info.json` "gate".

FCEUX's first frames differ because its power-up palette RAM contents
differ. Its frame alignment also varies by a couple of frames between
launches: its Qt build runs emulation on its own thread. Mesen2 is the gate.

The release tarball has also run headless on the owner's Alienware (Bazzite,
glibc 2.43). Its frame hashes are bit-identical to the build machine's, and
the core takes 1.2–1.45 ms per frame there (about 700–840 fps).

`mm-headless --cdl FILE` accumulates a code/data log over runs: one byte
per PRG byte, bit 0 executed, bit 1 read as data, in FCEUX's `.cdl` PRG layout.
The three traces touch 15,195 bytes of 6502 code and 38,856 bytes of data.

Reference-emulator notes:

- The Mesen2 2.1.1 release binary aborts with `std::bad_cast` on current
  distributions (a known upstream issue). Build it from source, non-AOT, and
  point `MESEN` at it. Its test runner needs `xvfb-run` and a `settings.json`
  (`ref-run.sh` installs `port/tools/mesen-settings.json` into
  `build/port-ref/mesen-home`). That enables Lua I/O and both NES
  controllers.
- In Mesen 2.1.1, `emu.setInput(t, 1)` also overwrites port 0's buttons, so
  the dumper sets port 1 first.
- The test runner stops after 100 s by default. `ref-run.sh` passes
  `--timeout=36000`. Mesen can hang on exit after `emu.exit()`, and its
  output is complete at that point.
- FCEUX runs from `.tools/fceux` with its bundled libraries and Qt plugins.

### Gameplay traces

`port/tools/tracegen` builds a trace closed-loop. It steers the game's pointer
(OAM entry 1) to screen positions from live sprite data and presses buttons.
It writes a plain `.mmin`, so the port and the reference emulator replay the
same inputs. `port/traces/newgame.txt` is the script: power on, intro, pick
two kids, START, the opening dialogue, then a walk command on the verb UI.
`port/traces/newgame.mmin` is its output (8936 frames).

```sh
build/port/tracegen rom/original.nes < port/traces/newgame.txt > port/traces/newgame.mmin
```

`mm-headless --save-state N:FILE` / `--load-state FILE` take whole-machine
snapshots. Reloading one reproduces the continuation frame for frame, which
is tested.

## Dev menu

This follows DEV_MENU_SPEC. It is **off by default**; `MM_DEV_MENU=1` or
`[dev] dev_menu = on` enables it, and `MM_CHEATS=0` disables it even when
requested. Open and close it with **F8** or **Back+Start on the same pad**.
With the opt-in on, Select/Start are held back from the game for 6 frames,
so the combo never reaches the game. Navigate with the D-pad or stick
(edge-triggered, no auto-repeat), A to confirm and B to go back, or with the
arrows, Enter and Esc on the keyboard. The overlay is drawn on the presented
copy of the frame. The game keeps running while the menu is open, but it
receives no input, and after the menu closes held buttons must return to
neutral before input reaches the game again.

| Page | Contents |
| --- | --- |
| Warp, Finish Area, Cheats | No entries yet. Each needs verified game state (SCUMM room and variable locations from the decomp and route recordings), so each shows a note and no selectable rows |
| Options | Fast-forward, speed (2×/4×/8×/max), screenshot, quick save, quick load, pixel aspect (8:7/square), overscan crop, help page |

Quick save/load (also F11/F12 with the opt-in) use one host-side slot:
`~/.local/share/maniac-mansion-port/quicksave/slot1.mmst`. It holds a
whole-machine snapshot tied to the build and ROM, and the battery save format
is untouched. Because it is a full snapshot, it is safe at any point. The
spec's "field only" rule exists for ports that serialize game state.

Keys that always work: F6/L3 fast-forward toggle, Backspace/R3 (hold),
F10 screenshot, Alt+Enter fullscreen and Esc quit. Only with the opt-in:
F1 help, F7 speed, F8 menu, F11/F12 quick save/load.

`port/tests/test_devmenu.sh BUILD ROM OUT` is the gate. It runs windowed
under Xvfb and uses the harness-only `MM_DEV_KEYS="frame:KEY,…"` input, which
is inert unless the variable is set.

| Check | Result |
| --- | --- |
| All-off identity: menu opened, navigated and closed during `smoke.mmin`, every frame hash equal to a run with the opt-in off | 900/900 identical |
| Opens (F8), off by default (scripted keys inert), hard disable (`MM_CHEATS=0`) | pass |
| Quick save at frame 300, quick load at 600: the run replays the original from the saved frame | 200/200 frames identical |
| Screenshots (root, options, empty group) | `build/devmenu-test/menu-*.png` |

Physical controllers (the owner's Legion Go 2 and Steam Deck) are **not
tested yet**. Virtual-device tests don't count as hardware passes.

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
