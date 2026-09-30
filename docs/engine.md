# USA engine investigation

This is partial analysis of the owner ROM identified in [rom.md](rom.md).
Reviewed instruction ranges are generated locally with da65 and reassembled with
ca65. Their exact rebuild passes full-file verification. An initial FCEUX counter
trace confirms execution of selected routines; static findings and
source-comparison hypotheses remain distinct. CPU addresses below refer to bank
0f at $C000 unless stated.

## Reviewed USA routines

| Symbol | CPU range, end exclusive | Static finding |
| --- | --- | --- |
| `reset_entry` | $D799–$D820 | Hardware initialization, title-stage call, later RAM clear setup. |
| `fill_memory` | $D820–$D83A | Fill a 16-bit pointer/count range with X; modifies A/Y and zero-page pointer/count. |
| `switch_prg_bank` | $DF85–$DFBC | Cached bank selection with NMI coordination and deferred bank-0 service. |
| `nmi_dispatch` | $E362–$E365 | Indirect jump through RAM $72D9/$72DA. |
| `nmi_game` | $E365–$E410 | Register save/restore, optional OAM DMA, bank-0 callbacks and sprite-zero scroll split. |
| `mmc1_write_prg` | $FFA0–$FFB4 | Five serial writes to $E000, LSB first; A shifts right four times. |
| `mmc1_write_control` | $FFB8–$FFCE | OR A with $0E, then five LSB-first writes to $8000. |
| `mmc1_initialize` | $FFD0–$FFF9 | Reset serial latch, configure control, set CHR bank register to zero. |

These eight ranges total 473 bytes. The progress denominator is the entire
262,144-byte PRG, not an assumed code size. Opaque gaps remain unclassified and
are never credited as understood data or functions.

The final vectors are NMI $E362, reset $D799, IRQ $FFFF. The IRQ target is not yet
classified as an executable handler. MMC1 control initialization selects $0E:
16 KiB PRG switching with the last bank fixed at $C000, 8 KiB CHR mode, vertical
mirroring. This corroborates the linker bank interpretation. Mapper writes are
kept as assembly because their instruction ordering and interrupt interleaving
are part of the preserved behavior.

### Reset and memory fill

Reset clears decimal mode, disables maskable IRQs, sets SP=$FF, waits for two
PPU vblank observations through $2002, then initializes MMC1. It invalidates the
cached PRG bank at $6F04, selects bank 0, sets the NMI dispatch pointer to $9E01,
and calls bank-0 $9E04. The function of that title-stage code is a source-guided
hypothesis pending analysis. Execution then reaches $D7C2, resets CPU state again,
waits for vblank, disables PPU rendering/NMI and APU channels/DMC, and writes $C0
to $4017. A 256-iteration loop clears CPU RAM $0000–$07FF.

It next calls `fill_memory` with pointer $6000, count $12D9 and X=0, clearing
$6000–$72D8. This does not establish the complete save layout: the battery RAM
continues to $7FFF. The fill routine increments its pointer, decrements its
16-bit count and stops at zero. Its current callers supply nonzero counts; a
zero input would underflow and requires preservation in any translation.
Later boot at $D83A sets the gameplay NMI pointer to $E365; that later boot range
is observed but not yet credited as reviewed.

### NMI and bank switching

The NMI vector enters a RAM-selected dispatcher, allowing title and gameplay
handlers. Gameplay preserves A/X/Y. When $72B4 is zero, it copies OAM page $02
through $4014; otherwise it takes a delay loop. If $710D OR $72D8 is zero, it
temporarily selects bank 0, calls $834A, and restores bank $6F04. Callback semantics
remain unresolved. When $6D41 is nonzero, it writes scroll, delays, polls sprite-0
hit in $2002 bit 6, then performs two more scroll changes with explicit delay
loops. CPU cycle timing and sprite hit behavior must be tested on an emulator.

A second gated callback sets $72D8 to $FF, selects bank 0, calls $8304, restores
the cached bank and clears $72D8. The NMI copies $710D into $72D7 and increments
$B8 and $06 before restoring registers and RTI. These flags coordinate with
`switch_prg_bank`: the latter skips an already-cached bank, raises $710D, updates
$6F04 and writes MMC1, optionally services bank-0 $8C07 while preserving X/Y, then
clears $710D/$72D7. Thus temporary NMI bank changes intentionally do not change
the mainline cached bank.

The footer writer shifts A and performs five stores. Initialization writes $FF
to the four MMC1 address regions, writes control $0E and writes zero five times
to $A000. A C compiler is unlikely to reproduce those instructions, registers,
cycles or exact bank-switch ordering. The chosen matching approach is documented
assembly, generated from the owner ROM using reviewed range recipes. Higher-level
C translations will need separate behavioral tests and will not count as matching
merely because their effects look similar.

## VM and controller discovery

Subsequent static analysis found these additional matching ranges (end exclusive):

| Symbol | Bank / CPU range | Finding |
| --- | --- | --- |
| `vm_dispatch` | 0f / $C423–$C44C | Opcode fetch and RAM-resident JSR trampoline. |
| `vm_fetch_word` | 0f / $C44C–$C44F | Calls byte fetch and falls through for a second byte; does not itself combine them into a 16-bit return value. |
| `vm_fetch_byte` | 0f / $C44F–$C478 | Banked byte fetch using a self-modified absolute LDA in RAM. |
| `vm_restore_pc` | 0f / $C478–$C491 | Reconstructs absolute fetch address from script base and saved slot offset. |
| `vm_save_pc` | 0f / $C491–$C4AA | Saves absolute fetch address minus script base into the current slot. |
| `vm_jump_relative` | 0f / $C7CD–$C7E9 | Reads a little-endian 16-bit displacement and adds it to the post-operand script PC modulo 65536. |
| `vm_stop_object` | 0f / $C7E9–$C807 | Ends the current slot, calling a global-script removal helper for kind 2; sets current slot to $FF. |
| `read_controllers` | 0f / $FB68–$FBAE | Serial polling of both controllers and latched press events. |
| `consume_a_press` | 0f / $FBAE–$FBB7 | Returns and clears the $710E press latch. |
| `consume_menu_press` | 0f / $FBB7–$FBBE | Returns and clears the $2F press latch. |
| `opcode_handler_high` | 0f / $FD97–$FE97 | 256 high bytes of native opcode handler addresses. |
| `opcode_handler_low` | 0f / $FE97–$FF97 | 256 low bytes of native opcode handler addresses. |
| `nmi_bank0_services` | 00 / $8304–$834A | Countdown/script-slot service, sound/input, conditional cursor-related callback. |
| `ppu_command_dispatch` | 00 / $834A–$8378 | Checks a pending graphics queue and dispatches through a table to a RAM indirect-JMP target. |
| `sound_tick` | 00 / $8C07–$8C49 | Guarded sound countdown/sequencer service and sound stream pointer advancement. |

Together with the first ranges these total 934 code bytes plus 512 identified
opcode-table bytes: 1,446 / 262,144 PRG bytes (0.5516%), 23 symbols. Table contents
are extracted locally, named separately from code, and are not stored in git.

The current script slot index is $6314; a negative value stops `vm_dispatch`.
Otherwise it calls `vm_fetch_byte`, saves the opcode at $6CAB and indexes the two
handler-address tables. It writes $20 (JSR), the selected low/high address, and
$60 (RTS) into $7EE1–$7EE4, then calls $7EE1 and loops. This explains why searching
only for indirect-JMP instructions did not locate the VM dispatcher. RAM near
$7EDD is executable scratch inside the cartridge's battery RAM.

Byte fetch reads the script bank at $6CB1, checks it is <=$0F, and selects it using
the reviewed mapper helper. It writes a self-modified `LDA absolute; RTS` stub at
$7EDD–$7EE0, calls it and increments the operand at $7EDE/$7EDF. Script base is
$6CA9/$6CAA. Save/restore converts between that absolute operand and per-slot
offset arrays $6329 (low) / $6315 (high). These routines establish addressing;
script slot lifecycle, scheduling and operand-variable rules remain unreviewed.

| Opcode | Handler in bank 0f | ScummVM comparison meaning |
| --- | --- | --- |
| $00 | $C7E9 | Stop object code |
| $01 | $CFAF | Put actor |
| $14 | $CDB3 | Print |
| $18 | $C7CD | Relative jump |
| $1C | $CEF3 | Start sound |
| $AB | $D75C | Switch costume set |

Handler addresses come directly from the owner ROM's tables. Relative-jump and
stop-object handler instructions are now reviewed; actor, print, sound and costume
names remain source-comparison hypotheses pending instruction/trace review.
Preserve all 256 opcode entries and flag variants; do not collapse
the table based only on similar high-level names.

Controller polling runs X=1 then X=0. It copies previous raw states $02/$03 to
$04/$05, strobes $4016 with 1 then 0 for each controller, then shifts in eight
bit-0 reads from $4016+X. This places A in bit 7, B in bit 6, Select in bit 5,
Start in bit 4, Up in bit 3, Down in bit 2, Left in bit 1, Right in bit 0.
For controller 1, a transition from no $70 bits to any $70 bits latches that mask
at $2F; a newly pressed A latches $80 at $710E. Consumer routines clear each latch.
This edge behavior is exact static logic; cursor acceleration/repeat and how
each action consumes it remain to be traced.

The bank-0 NMI service calls fixed-bank $C78C, sound $8C07 and controller $FB68,
then conditionally invokes $913F or hides an OAM entry. $C78C appears to service
script-slot countdowns and $913F appears cursor-related; those callee semantics
remain hypotheses. The PPU command dispatcher checks $6EFF and queue indices
$6EBC/$6EBD, indexes queued commands at $6EBE and writes a handler address to
zero-page $C5/$C6 before jumping through it. Queue opcodes and data formats remain
unclassified.

The sound tick checks guard $7261, chooses countdown slot $7264 or $7265 and calls
$8C49. That downstream sequencer writes $4015 and APU channel registers (observed,
not yet credited). The tick passes the third stream pointer $7272/$7275 through
$E5/$E6 to $8CE2, adds consumed-byte count $7278, then clears that count and guard.
Complete command meanings, priority and tempo require additional analysis.

## Rooms and assets: reference findings, addresses pending

ScummVM treats this as v1 using early shared opcode handling. Follow
`script_v2.cpp` and inherited handler sources when
testing opcode flags, variable addressing, branch offsets and script scheduling.

Room resources and PPU graphics are distinct from executable 6502 code. ScummVM's
NES adapter has region-specific resource ranges; its `GdiNES` handles room/object
tile data, attributes, palettes and masking. Costume descriptions select frame
length/offset tables and sprite data, ultimately using pattern-table tiles and
palettes. Charset rendering reads two tile planes through a character translation
table. These are leads for local extraction/classification, not imported assets.
No room, costume, string or charset data is published here.

## Sound, input and save: work still required

Sound: compare bank-0 callbacks against `Player_NES` sequencing and APU register
writes. Locate channel state, music commands and effects; measure NMI/frame
timing, channel priority and region clocks before making native claims.

Input: the basic polling/edge routines are reviewed above. Determine cursor
acceleration/repeat, verb selection and cutscene skipping. ScummVM's
controller adaptations are useful references but cannot replace cartridge input
traces.

Save: the USA target declares battery RAM and has save/load behavior rather than
the Japanese engine's password system. Locate the cartridge save signature,
copy/checksum paths and retained RAM region; test power cycling, save, load and
corruption with local emulator saves. Reset's partial RAM clear alone cannot
define the persisted state. No save layout is currently claimed.

## Evidence required for the eventual native port

### Initial execution evidence

FCEUX 2.6.5 (`ea6ed69b874e3ae94072f1b4f14b9a8f0fdd774b`, Ubuntu package
2.6.5+dfsg1-2build4), NTSC, completed two 900-frame runs using
`tools/trace-smoke.lua`: one of the untouched owner ROM and one of the rebuilt
ROM. The script power-cycles the emulator and applies the same specified
Start/A/Right input sequence. It never changes RAM or CPU registers. Sound output
was disabled, while APU register writes were still observed.

Both JSON summaries matched exactly, SHA-256
`274e2936f77c13cb737dc147100875246dc096df171a87168f80bed1268bf150`.
Final PC was $C126. Counts were reset 1; MMC1 initialization 2; PRG serial writer
2,475; NMI dispatcher 875; gameplay NMI 508; controller reader 507; VM dispatcher
448; byte fetch 1,312. Observed writes were PPU 249,173; APU 1,046; controller
strobe 3,280; watched mapper register addresses 12,403.

Outputs and emulator logs remain under ignored `build/`. These are selected
CPU-address hit counts and register-write counts, not a physical-bank code/data
log, cycle trace, framebuffer/audio comparison, or full game playthrough. The
original and rebuilt ROMs also match every byte; this trace adds evidence that
the image executes, while native-equivalence work remains outstanding.

To reproduce with an installed FCEUX and display (or wrap in `xvfb-run -a`):

```sh
MM_TRACE_OUTPUT="$(pwd)/build/source-smoke-trace.json" \
  fceux --no-config 1 --sound 0 --loadlua tools/trace-smoke.lua rom/original.nes
MM_TRACE_OUTPUT="$(pwd)/build/smoke-trace.json" \
  fceux --no-config 1 --sound 0 --loadlua tools/trace-smoke.lua build/game.nes
```

Use the same emulator configuration and initial battery-save contents for any
comparison. The script defaults to 900 frames; `MM_TRACE_FRAMES` can override it.
The trace API follows the [FCEUX Lua documentation](https://fceux.com/web/help/LuaFunctionsList.html).

### Remaining comparisons

Capture physical-bank code/data logs and detailed traces from reset, title/selection, representative
room transitions, actor movement, text, music/effects, input and save/load. Record
physical bank and CPU address together. For every translated subsystem compare
state transitions, framebuffer output, audio writes/timing and save bytes against
the untouched USA ROM. Preserve known quirks and regional scripts; enhancements
are explicit options and off by default. This matching baseline does not implement
a native engine translation; current port work is maintained separately in the
private port repository.
