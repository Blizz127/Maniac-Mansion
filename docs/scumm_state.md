# USA SCUMM state map for warp investigation

Target: the USA PRG SHA-1 `8a8bbecc77fdf59826257754f357d38a7f825971`.
CPU addresses refer to fixed PRG bank 0f ($C000–$FFFF), unless stated otherwise.
Routine names here are investigation labels, not original symbols.

**VERIFIED (by trace)** means the described behavior was observed in a cartridge
execution trace. **HYPOTHESIS** means static disassembly supports the interpretation
but the corresponding transition has not yet been observed. A routine hit alone
only verifies execution, not all proposed semantics. Local listings, RAM dumps,
framebuffers, game scripts and ROM-derived tables are not public artifacts.

This is an address map, not permission to implement a warp by overwriting RAM.
No arbitrary destination warp has yet passed an equivalence or gameplay test.

## Room and room-change path

| Status | Address / label | Interpretation and static evidence |
| --- | --- | --- |
| VERIFIED (by trace; observed roles) | `$63C6`, `current_room` | Logical room ID. `room_load` stores its incoming X here at `$C185`. Actor-room checks at `$C34D`, `$D264`, `$E153` compare against it. |
| VERIFIED (by trace; observed roles) | `$6104` low / `$6004` high, `scumm_var_room` | Script-visible variable 4. Stores at `$C15B/$C160` and `$C188/$C18D` mirror the logical room with high byte zero. The VM's normal variable words have low bytes at `$6100+index`, high bytes at `$6000+index` (`$C6A6`). |
| VERIFIED (by trace; observed roles) | `$63C5`, `room_resource_id`; `$6124/$6024`, variable 36 | Resolved resource room ID. `$C190–$C1A0` keeps nonnegative logical IDs, but resolves negative IDs through `$6891,X` before storing the resource ID (**HYPOTHESIS**, negative-ID branch not traced). Do not assume logical and resource room IDs always match. |
| VERIFIED (by trace; observed roles) | `$C13A`, `room_change` | X = requested logical room. Calls exit script `$C38A`, room-slot cleanup `$C522`, other cleanup `$D365/$EE32`; detaches active actors via `$D9A7`; then calls `$C185`. Sets camera bounds, installs actors, marks redraw, schedules entry script `$C360` and global script 5 via `$C582`. |
| VERIFIED (by trace; observed roles) | `$C185`, `room_load` | Writes logical and resource IDs; calls `$C1C6` header loader, `$C24A` object loader and `$C310` room-related setup. Room 0 was observed during intro; static disassembly shows a special stack-unwind path at `$C1B6`; this is not a general-purpose C-call ABI. |
| HYPOTHESIS | `$C1C6`, `room_header_load`; `$C229`, `room_resource_pointer` | Loads header into `$63B5…`, resolves pointers and room entry/exit offsets. `$DF76` selects the room's PRG bank; descriptor indexing uses resource ID × 4 at `$DC95`. |
| VERIFIED (by trace; observed roles) | `$CBC1`, `op_load_room` | Reads an immediate or variable byte using `$C691`; if different from `$63C6`, selects bank 0, calls bank-0 `$818F`, then passes X to `$C13A`. |
| VERIFIED (by trace; observed roles) | `$CBD9`, `op_load_room_with_ego` | Richer transition path: reads destination object, destination room and coordinates; moves the selected actor into the new room; calls `$C13A`; resolves object position/direction; follows the actor and starts script 5. See `$CBEC–$CC99`. |

`$6394` is **not** a current-room candidate: it is `$638D+7`, inside the
20-entry script-kind/flags array. The new cartridge trace independently confirms yard-to-porch scrolling within
room `$01`: camera `$2D`→`$1A`, left `$1D`→`$0A`, right `$39`→`$26`, while
`$63C6/$63C5/$6104` all remain `$01`. Entering this yard from the initial
roadside scene is a real `$2C`→`$01` room transition, via `$CBD9`. These are
different operations; do not use a screenshot change as the room-change test.

## Camera and render scroll

| Status | Address / label | Interpretation and evidence |
| --- | --- | --- |
| VERIFIED (by trace; observed roles) | `$6C33`, `camera_x`; `$6102/$6002`, variable 2 | Current horizontal camera center in engine units. Main loop copies `$6C33` to `$6102` at `$C069`. `$E001` nudges it toward `$6C36`, clamped by `$6117/$6118`. |
| VERIFIED (by trace; observed roles) | `$6C31`, `viewport_left`; `$6C32`, `viewport_right` | `$E09E–$E0AD` derives left = camera − `$10`, right = left + `$1C`. These values have different margins from the PPU pixel scroll. |
| VERIFIED (by trace; observed roles) | `$6C34`, `camera_mode`; `$6C35`, `camera_follow_actor`; `$6C36`, `camera_target` | `$D256` selects mode 2 and the followed actor. `$D237/$D243` select mode 3. `$E01D…` handles actor-follow and target tracking. |
| HYPOTHESIS | `$6117/$6017`, variable 23; `$6118/$6018`, variable 24 | Horizontal camera limits. `$C166–$C171` initializes low-byte bounds to `$0E` and room width (`$63B5`) − `$0E`. High-byte behavior needs separate review. |
| HYPOTHESIS | `$E0B1`, `camera_set_target`; `$E49D`, `camera_to_ppu_scroll` | Target routine may snap current camera if distance exceeds `$0E`. PPU conversion clamps camera − `$0E` to 0…31 and multiplies by 8 into `$07`. |
| HYPOTHESIS | `$07/$08`, `ppu_scroll_x/y`; `$6D41`, `split_scroll_enable` | `$E365` gameplay NMI uses these for PPU scroll writes and sprite-zero split timing. They are render outputs; writing them does not change a room or actor. |

## Objects, inventory and kids

| Status | Address / label | Interpretation and evidence |
| --- | --- | --- |
| VERIFIED (by trace; observed roles) | `$660A+object_id`, `object_state_owner` | Global object state/owner byte, correlated with actual mat/key/door actions. `$C701` computes its address from a 16-bit object ID. `$CD35` reads low nibble; `$CD41` preserves high nibble while setting owner. `$CB86–$CB90` sets ownership to selected actor and state bits during pickup. Initialization `$C3B4` copies `$0307` (775) bytes to `$660A–$6910`; valid object IDs and each state bit still need behavioral tests. |
| VERIFIED (by trace; observed roles) | `$6260+slot`, `inventory_object_id` | 60-entry object ID table (indices 0…59; allocation policy is separate). Pickup `$CB4B` stores ID at `$CB79`; `$F927` searches inventory when owner nibble is not `$0F`. Slot occupancy and high object-ID support need tests. |
| VERIFIED (by trace; observed roles) | `$629C+slot` low / `$62D8+slot` high; `$6A22+slot` bank | Parallel 60-entry inventory object resource pointers and bank. Pickup stores them at `$CB69/$CB6E/$CB74`; `$C4F1` reconstructs inventory script base from them. Owner change/removal clears ID and pointers at `$CD80…`. |
| VERIFIED (by trace; observed roles) | `$63D9`, `room_object_count`; `$64F2+i/$64CF+i`, object ID low/high | Current room object cache. `$C24A` loads room objects; fields are stored at `$C2B2/$C2B9`. Other 35-stride arrays start `$63DA`, `$63FD`, `$6420`, `$6443`, `$6466`, `$6489`, `$64AC`, `$6515`, `$6538`, `$655B`, `$657E`; individual field meanings need transition tests. |
| VERIFIED (by trace; observed roles) | `$6100/$6000`, variable 0, `selected_actor` | Ego/selected kid ID. `$CBD9…` indexes actor room and direction with `$6100`. Actor IDs must not be confused with active-render slots. Chosen-party/progression/captured-kid script variables are not yet identified. |
| VERIFIED (by trace; observed roles) | `$6B5D+actor`, `actor_room`; `$6B91+actor`, `actor_x`; `$6B77+actor`, `actor_y` | Persistent actor tables, initialized with indices 0…25 at `$C012`. `$E114` writes all three; getters `$D38B`, `$D399`, `$D3B2` read them. Kid-specific IDs require trace correlation. |
| HYPOTHESIS | `$6BDF+actor`, direction; `$6BC5+actor`, costume; `$6B43+actor`, costume/visibility-related selector | Put/direction/costume opcodes use these; exact `$6B43` semantics remain unresolved. |
| VERIFIED (by trace; observed roles) | `$6D22+actor`, `actor_active_slot`; `$6D1A+slot`, `active_slot_actor` | Persistent actor→render-slot and inverse mapping. Four active slots (0…3), negative value means detached. `$D9A7` removes a mapping; `$D9C8/$D9E5` allocates one. |
| VERIFIED (by trace; observed roles) | `$6CBE+slot`, active X; `$6CC2+slot`, active Y; `$6CCE+slot`, walk/animation flags | Active actor state, four entries per array. `$D9A7` copies active coordinates back to persistent tables before detach; `$E114` synchronizes both when placed. Coordinate/mapping roles were observed in movement; individual flag bits and full walk completion semantics remain **HYPOTHESIS**. |

Moving a kid requires persistent actor room/position plus active-slot rebuild,
walking/costume state, camera and scripts. Object ownership, inventory resources,
party choice and progression cannot be inferred from current room alone.

## Script slots and candidate idle boundary

| Status | Address / label | Interpretation and evidence |
| --- | --- | --- |
| VERIFIED (by trace; observed roles) | `$6314`, `current_script_slot` | Signed byte; `$FF` means no executing slot. `$C423` returns on negative. Scheduler `$C3FD` runs slots whose status equals 2. |
| HYPOTHESIS | `$6315+i` high / `$6329+i` low, `script_pc_offset` | 20-entry arrays. `$C478/$C491` restore/save fetch pointer relative to script base `$6CA9/$6CAA`. |
| HYPOTHESIS | `$633D+i`, `script_status` | 0 = inactive; 1 = delayed; 2 = runnable; bit 7 suspends. `$C78C` ticks delayed slots; `$D19A/$D1BB` suspend/resume slots. Main scheduler scans indices 1…19; slot 0 is also used by special room scripts. |
| HYPOTHESIS | `$6351+i/$6365+i/$6379+i`, delay high/mid/low | 20-entry 24-bit countdown representation. `$C78C` increments low→mid→high and changes status 1→2 on wrap. Exact frame units need NMI correlation. |
| HYPOTHESIS | `$638D+i`, `script_kind_flags`; `$63A1+i`, `script_id` | 20-entry arrays. Kind masked with `$3F`: 2 global script, 1 room script, 0 inventory object script. Bit 6 affects freezing. `$C4AA` selects resource base/bank according to kind. |
| HYPOTHESIS | `$6CA8`, scheduler cursor; `$6CA9/$6CAA`, script base; `$6CB1`, script bank | Execution scratch. `$7EDE/$7EDF` is the absolute fetch address in a RAM LDA stub. Handler JSR trampoline occupies `$7EE1–$7EE4`; these executable bytes are not ordinary state variables. |
| VERIFIED (by trace), execution only | `$C423`, `vm_dispatch`; `$C44F`, `vm_fetch_byte` | Surviving 900-frame untouched-ROM smoke trace: 448 and 1,312 hits respectively. This does not verify the full slot lifecycle. |
| HYPOTHESIS | `$C870`, `vm_yield` | Saves slot PC, sets `$6314=$FF`, returns. A VM yield is not sufficient proof that a warp is safe. |
| VERIFIED (by trace), observation only | `$C126`, `frame_wait` | Final PC in both surviving smoke runs is `$C126`. Static path reaches this wait after actor/render work and `$E449` graphics queue drain. This proves an observed wait PC, not safe warp injection. |

**HYPOTHESIS — proposed warp boundary:** intercept the mainline immediately before
`$C049` (next main-loop iteration), or at `$C126` after checking the state. Require
no executing script (`$6314=$FF`), no room-change call on the CPU stack, no nested VM caller (`$6C49=0`), no pending
PPU queue (`$6EBC=$6EBD`, `$6EFF & 2 = 0`), and no cutscene/dialogue/walking operation
that the warp would interrupt. Related control bytes include `$6B0A` (user-state flags, `$D2B6`) and the
cutscene candidates `$6B08/$6B09`
(saved cutscene slots), `$6B06/$6B07` (override PC), `$6C6A` (nested script-control
stack index; distinct from `$6C49`, the VM caller stack). These are clues, not a complete allowlist of field state.

The safe **allowed** warp point is still **HYPOTHESIS**. In the intro at frame
5000, the CPU is at `$C12B` with current slot `$FF` and drained graphics queue,
yet cutscene slot `$6B08=6` and user state `$6B0A=0`. During field play, observed
values are `$6B08=0`, `$6B0A=$F0`, `$6C49=0`, `$6C6A=$FF`. Thus even the wait
PC plus `$6314=$FF` plus empty queue is **insufficient**. Requiring user input
enabled (`$6B0A & $10`), no cutscene, neutral input, no walking, zero bank/NMI
guards `$710D/$72D8`, and execution at a mainline boundary is a proposed gating
rule; arbitrary warp safety still needs port-side injection tests. An interrupt can be inside
NMI even when a frame snapshot looks idle. Do not replace the PC with `$C13A`
without preserving the stack, return continuation, bank cache and interrupt
coordination. Prefer a scheduled engine-level operation that follows the normal
room/ego transition path. Validate the first warp against a natural door entry,
including exit/entry scripts, actor placement, inventory, camera, video/audio,
continued input and return travel. No raw-RAM warp should be exposed on this map
alone.

## Reproducible local evidence

The initial matching build and smoke procedure are in [engine.md](engine.md).
`tools/trace-state.lua` replays a local `.mmin` input trace, then optionally reads
pointer commands (`seek X Y [sprite]`, `press BUTTON [hold_frames]`, `wait N`, `dump NAME`).
It records address events, routine hit counters, snapshots and local RAM/screens.
It never writes CPU memory/registers or applies cheats. API behavior follows the
[FCEUX Lua reference](https://fceux.com/web/help/LuaFunctionsList.html).

Set `MM_STATE_DIR`, `MM_STATE_INPUT`, optional `MM_STATE_STEPS`; run FCEUX with
`--no-config 1 --sound 0 --loadlua tools/trace-state.lua` and your own USA ROM.
Use an isolated emulator profile, NTSC timing and known battery contents. Run
heavy traces through the blizzpc runner; keep every output under ignored `build/`
or the owner's scratch directory. The opening trace alone does not establish a safe warp. The natural route
continuation provides the separate room-transition evidence below.

## Cartridge trace results (2026-09-30)

FCEUX 2.6.5, NTSC, sound output disabled, input-only replay on blizzpc through
the guarded runner. No RAM/register writes, patches or cheats. The initial
9056-frame run completed with exit 0; intro room transitions were observed
at logical IDs `$00`→`$21`→`$31`→`$00`→`$2D`→`$00`→`$2C`.
The entry/loading hooks `$C13A/$C185` each fired six times. Event log SHA-256:
`e381713b2cf878791ce6826ae355cfa080f3f936c23e25ed8b051a7372e0c727`.

The 15502-frame gate/doormat continuation also completed with exit 0
(job `20260930-130146-mm-scumm-key-1814351`). At frame 10711 `$CBD9` executes
in room `$2C`; frames 10745/10746 enter `$C13A/$C185` with X=`$01`. By frame
11562 all three room mirrors are `$01`; a subsequent walk changes camera
without a room load.

Observed table correlations (metadata only; full dumps remain local): actor IDs
1/2/3 initially have room `$2C` and active slots 0/1/2, with inverse mapping
matching them. Selected actor variable 0 is 3. Inventory slots 58/59/57 hold
object IDs 49/47/46 with owner nibbles 1/2/3 respectively. This confirms the
shared actor and owner-index relationship; it does not identify every kid's
progression or chosen-party variables. Persistent and active coordinates can
differ after walking, so use the active coordinate when the actor has a slot.

Pulling the doormat in room `$01` changed object 54 (mat) from `$0F` to `$8F`.
Object 55 is the key; object 61 is the front door. Key visibility depends on
the mat state as well as the key byte; it is not a simple standalone visibility
bit on the key. These IDs are route metadata. High-bit meanings beyond the
observed actions remain **HYPOTHESIS**.

The complete front-door route passed on the same cartridge emulator (job
`20260930-132356-mm-scumm-door-center-2441114`, exit 0, 18093 frames). Key
pickup executes `$CB4B` at frame 14885: inventory slot 56 becomes object 55,
and `$660A+55` changes `$1F`→`$B3` (owner 3). Using the key opens door 61,
changing `$4F`→`$8F` without changing the room. Then:

| Frame | Observed transition |
| --- | --- |
| 16898 | `$CBD9` room-with-ego handler, current room `$01` |
| 16931 | `$C13A`, requested room X=`$0A`; then `$C185` |
| 18093 | Hall frame: `$63C6=$63C5=$6104=$6124=$0A`; actor 3 room `$0A` |

Final camera is `$0E`, current script slot `$FF`, VM caller depth 0, queue
indices equal, cutscene slot 0, user state `$F0`. This is a **VERIFIED observed
field idle state** at PC `$C12B`; it still does not certify injected warps. The
natural door transition provides the state-change reference for that next test.
Event log SHA-256:
`96d999a1c838f56d047d3d9a84f9780654e5feb7701e4f14331e7a46c29c23a6`.

Reproduce with your own ROM, an installed FCEUX and display (or Xvfb):

```sh
mkdir -p build/state-front-door
MM_STATE_DIR="$PWD/build/state-front-door" \
MM_STATE_INPUT="$PWD/port/traces/newgame.mmin" \
MM_STATE_STEPS="$PWD/tools/trace-front-door.txt" \
  fceux --no-config 1 --sound 0 --loadlua tools/trace-state.lua rom/original.nes
```

Heavy replay belongs on the blizzpc runner. The existing historical input trace
is only read; no port code or trace is changed. Earlier exploratory pointer
coordinates sometimes missed UI cells or object boundaries; the published
continuation uses the validated door-center target. It holds each button for
the requested duration and explicitly releases all buttons between presses.
RAM, graphics, intermediate attempts and detailed logs remain local.
