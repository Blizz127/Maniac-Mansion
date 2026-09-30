# Research and credits

Research checked 2026-09-29 CDT before the first build. Searches covered matching
NES decompilations/disassemblies, NES SCUMM resources, ScummVM engine/tools,
ROMhacking.net and C64 references. No complete matching western NES engine
disassembly was located in the searched sources; this is a bounded search finding,
not a claim that none exists.

| Prior work | What exists / use | License found |
| --- | --- | --- |
| [ScummVM](https://github.com/scummvm/scummvm/tree/abd1ca3e317748489acd8143403615029f973ee3/engines/scumm) | Maintained NES resource adapter, interpreter, graphics, costume, charset and audio implementation. Compare formats and behavior; it does not rebuild the original 6502 engine. | Reviewed source headers: GPL-3.0-or-later. Credit ScummVM developers; copied/derived implementation requires preserving its terms. |
| [ScummVM tools](https://github.com/scummvm/scummvm-tools/tree/8665aa1d3221a6cc884026f5ed7144fac512e0e9/engines/scumm) | `extract_mm_nes` exposes resource groups; `descumm` aids script inspection. Extracted scripts are game data and must stay local. | Reviewed headers: GPL-3.0-or-later. |
| [gmarty / edo999: scumm-nes](https://github.com/gmarty/scumm-nes/tree/dee8361c0e1a3ac0e0afd313e9075073d8f8b3c6) | Resource explorer/editor for western regions and USA prototype. It explicitly excludes the runtime engine. | Blue Oak Model License 1.0.0, with ScummVM/Decoded ancestry stated in README. Review individual inherited portions before reuse; a top-level license does not resolve their provenance. |
| [gzip: Maniac Mansion Decoded](https://github.com/gzip/nes-6502-maniac-mansion-decoded/tree/413135fd4b7f0ea30edadb6d86dde591d16321f3) | ROM expansion/decompression and modifications, with useful address and compression references. Its output deliberately changes the retail ROM. | MIT at repository root; bundled third-party tools have separate notices. Asset/patch directories are not imported. |
| [pditincho: mm-explained](https://github.com/pditincho/mm-explained/tree/152940f7f3d27ffacd8dbd8c7d6516748c9c7a52) | Commented C64 engine, useful architecture comparisons; cannot establish NES instruction addresses. | No explicit license found in inspected repository metadata/tree. Reference only; no code imported. |
| [segrax: Maniac.Mansion.Disassembly](https://github.com/segrax/Maniac.Mansion.Disassembly/tree/0e4a04768e2a11eb869e1ba3c6a5b6429ef2c6c9) | v0 demo/retail game-script annotations and object/variable spreadsheets. README says it is not recompilable. | No explicit license found. Reference only; no scripts/spreadsheets copied. |

## NES-specific source guide

At the pinned ScummVM revision, `file_nes.cpp` maps region-specific resources and
generates virtual LFL resources/indexes. Its resource groups cover rooms, room
graphics, scripts, costumes, sprite descriptions/lengths/offsets/data/palettes,
sounds, charset and prepositions. Detection uses headerless PRG hashes. Offsets
must be interpreted in that scope rather than directly as CPU addresses.
[Source](https://github.com/scummvm/scummvm/blob/abd1ca3e317748489acd8143403615029f973ee3/engines/scumm/file_nes.cpp).

`detection_tables.h` identifies the western NES game as SCUMM version 1.
`script_v2.cpp` supplies shared early-engine opcode handling and NES branches;
there is no `script_v1.cpp` in this revision. Read inherited handlers too before
claiming a complete opcode specification.
[Detection](https://github.com/scummvm/scummvm/blob/abd1ca3e317748489acd8143403615029f973ee3/engines/scumm/detection_tables.h),
[VM](https://github.com/scummvm/scummvm/blob/abd1ca3e317748489acd8143403615029f973ee3/engines/scumm/script_v2.cpp).

Rendering references are `GdiNES` in `gfx.cpp`, NES title handling in `gfx_nes.cpp`,
`NESCostumeRenderer`/`NESCostumeLoader` in `costume.cpp`, and `CharsetRendererNES`
in `charset.cpp`. They describe NES tile planes and region/resource dependencies;
they remain comparison implementations until validated against cartridge traces.
[Graphics](https://github.com/scummvm/scummvm/blob/abd1ca3e317748489acd8143403615029f973ee3/engines/scumm/gfx.cpp),
[costumes](https://github.com/scummvm/scummvm/blob/abd1ca3e317748489acd8143403615029f973ee3/engines/scumm/costume.cpp),
[charset](https://github.com/scummvm/scummvm/blob/abd1ca3e317748489acd8143403615029f973ee3/engines/scumm/charset.cpp).

`players/player_nes.cpp` implements sound sequencing and APU access. Retain
cartridge timing evidence; do not assume a higher-level interpreter is a timing
oracle. [Audio source](https://github.com/scummvm/scummvm/blob/abd1ca3e317748489acd8143403615029f973ee3/engines/scumm/players/player_nes.cpp).

## ROMhacking.net and platform differences

Decoded's README points to [ROMhacking.net hack 7776](https://www.romhacking.net/hacks/7776/).
Direct RHDN retrieval was restricted in this environment. No standalone RHDN NES
SCUMM format document was verified; avoid crediting uninspected documentation.
The upstream tool source is the reproducible technical reference currently used.

The western NES version has controller-oriented interaction, PPU tile/sprite
rendering, cartridge banks, APU sound/music and battery-backed saves. These replace
PC/C64 hardware paths while retaining early SCUMM game structure. ScummVM's NES
branches and its resource adapter support these distinctions. A verified German
[cartridge board record](https://nescartdb.com/profile/view/995/maniac-mansion)
documents MMC1B3/SNROM, 8 KiB WRAM, 8 KiB VRAM and a battery; the owner's ROM header
and reviewed mapper initialization agree with that hardware family.

Douglas Crockford's first-person [account](https://www.crockford.com/maniac.html)
documents Nintendo-driven text/art edits and the USA versus later regional hamster
change. Preserve the owner's regional script content rather than restoring content
from another platform. The Japanese version uses a separate non-SCUMM engine,
as noted by the scumm-nes project; its graphics, engine and password behavior must
not be assumed to apply to this USA battery-save target.

No upstream engine code, assets, patches or game-script listings have been copied
into this project. New tooling is independently authored; prior work is credited
as investigation evidence. A future reuse decision must name the exact source
files and retain applicable notices.
