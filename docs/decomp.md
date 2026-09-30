# Matching decompilation baseline

Owner project in progress. The current USA build matches the owner's ROM exactly.
This is an initial bank-preserving disassembly, not a finished engine decompilation
or complete engine translation. See [research](research.md), [ROM identity](rom.md),
[engine notes](engine.md), and the [SCUMM state map](scumm_state.md).

## Build

Requires Python 3, Make and cc65 (`ca65`, `ld65`, `da65`). Tool binaries can be
installed normally or placed in the ignored `.tools/cc65/usr/bin/` directory.
The initial environment uses Ubuntu package `cc65 2.19-2build1`; the tools report
V2.18. No tools or upstream game content are vendored in source control.

Put your own USA ROM at `rom/original.nes`, then run:

```sh
make identify
make
make test
```

For a different local path, use `make identify ROM="/absolute/path/game.nes"`.
Identification pins the full input's CRC32, SHA-1, SHA-256 and path in ignored
`local/rom.json`. Subsequent builds reject changed input rather than silently
updating the target hash. Identification also checks the known retail payload;
unknown payloads cannot initialize the build manifest. Other western regions
have catalog metadata, but the reviewed engine ranges currently target USA only.

Every `make` extracts the exact header and sixteen 16 KiB PRG banks, generates
assembly for reviewed code ranges, preserves every remaining byte with local
`incbin` gaps, links `build/game.nes`, compares every byte, and checks all hashes.
The layout is fixed in `config/mmc1.cfg`; bank 0f uses CPU origin $C000 and the
switchable banks use $8000. No CHR-ROM exists in this profile; graphics stored in
PRG remain local. Extraction never rewrites or normalizes the header.

`build/game.map`, `build/game.lbl`, and `build/extracted/vectors.json` provide
local navigation. Reviewed instructions are in `build/disasm/prg00.inc` and
`build/disasm/prg0f.inc`.
`make progress` counts only nonoverlapping reviewed PRG-relative ranges with
documented evidence in `config/coverage.json`. Raw binary inclusion is zero
understanding credit. Engine addresses and metadata are shareable; generated
game content stays in ignored directories.

## Preservation and remaining work

The full-file match proves reconstruction, not gameplay equivalence of a future
native implementation. Next work requires code/data classification, the VM
dispatcher and handlers, graphics, sound, input and battery-save analysis, plus
emulator traces. Native work follows that evidence; enhancements default off.
Assembly is the matching baseline because cc65 C output is not expected to
preserve the original instruction sequence or timing.

No ROM, assets, extracted scripts, patches, emulator saves or generated assembly
may be committed. The decomp tooling and address documentation are public under the repository
license. Port development stays private; public binary releases occur at
milestones. Upstream reference licenses are recorded in the research notes.

## Public baseline guard

Stage only independently authored tooling, assembly include wrappers, configuration
and address/evidence documentation. Generated disassembly and game content stay
in ignored directories. Before committing and before pushing:

```sh
python3 tools/guard-staged.py --manifest local/staged-guard.json
# Commit only after zero rejected blobs. Before push, check those same blobs:
python3 tools/guard-staged.py --recheck local/staged-guard.json
```

The guard reads the Git index, checks filenames and every 32-byte PRG window,
and records only file hashes in a local manifest. Recheck fails if indexed files
changed. It also rejects public `port/` changes. The inherited port guard remains
unchanged; generated assembly is excluded separately, because text instructions
can reproduce game code without containing raw binary windows.
