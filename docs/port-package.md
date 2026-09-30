# Port release package contract

This is the Linux x86_64 release of the native Maniac Mansion (NES) port
(`port/`). Launchers can rely on everything below. No game data is ever in
the package: the user supplies their own ROM, which is verified by hash.

## Release on GitHub (`Blizz127/Maniac-Mansion`)

| Item | Value |
| --- | --- |
| Tag | `mm-rN-<sha8>`, where N is the release number and sha8 the first 8 hex digits of the commit (e.g. `mm-r1-1a2b3c4d`) |
| Asset | `maniac-mansion-port-<tag>-linux-x86_64.tar.gz` |
| Checksums | `SHA256SUMS` next to it, one line per asset: `<sha256>  <asset name>` |

## Archive layout

Everything is under one top-level directory, `maniac-mansion-port-<tag>/`:

```
maniac-mansion-port-<tag>/
  launch.sh                 entry point (use this, not bin/ directly)
  bin/maniac-mansion-port   native binary (x86_64, glibc >= build-info.json "glibc_min")
  lib/libSDL2-2.0.so.0      bundled SDL2 (launch.sh sets LD_LIBRARY_PATH)
  config.example.ini        all options, defaults commented
  README.txt  THIRD_PARTY.md  version.txt  build-info.json  MANIFEST
```

`build-info.json` contains `name`, `build_id` (= tag), `commit`,
`built_utc`, `platform`, `glibc_min`, `bundled_libs`, `contains_game_data`
(always `false`), `rom` (the accepted hashes below) and `gate` (the
verification results the build passed).

## launch.sh interface

```
launch.sh [ROM] [args...]          run the game
launch.sh --check-rom [ROM]        validate only: no window, no SDL, returns quickly
```

The ROM path comes from the first of these that is set:

1. arg 1 (any first argument that does not start with `-`);
2. the `MM_ROM` environment variable;
3. `[game] rom =` in `~/.config/maniac-mansion-port/config.ini`;
4. the last ROM that passed the check, remembered in
   `${XDG_DATA_HOME:-~/.local/share}/maniac-mansion-port/rom-path`;
5. `${XDG_DATA_HOME:-~/.local/share}/maniac-mansion-port/rom.nes`.

A launcher that stores the user's pick (for example `port.maniac-mansion.rom1`)
should pass it as arg 1 or as `MM_ROM`. The port only ever reads the ROM.
It never copies, modifies or uploads it.

If the ROM comes from steps 1–3 and fails the check, the port exits with the
error code below and shows a message box. If no ROM is found at all
(code 2), `--check-rom` exits immediately. A normal launch instead shows a
message box and keeps a window open so a `.nes` file can be dropped on it.
A launcher should therefore run `--check-rom` first.

User data (never in the install directory; the XDG variables can be
overridden per launch):

| What | Where |
| --- | --- |
| Battery save (8 KiB, raw) | `${XDG_DATA_HOME:-~/.local/share}/maniac-mansion-port/saves/maniac-mansion-usa.sav` |
| Config | `${XDG_CONFIG_HOME:-~/.config}/maniac-mansion-port/config.ini` |
| Log, screenshots | `${XDG_STATE_HOME:-~/.local/state}/maniac-mansion-port/` (`port.log`, `screenshots/`) |

The battery save is written atomically (a temp file, then rename) at most
every 2 s while it changes, and on exit.

## ROM validation

The supported ROM is **Maniac Mansion (USA)**, No-Intro
record 1360: NES 2.0 header, mapper 1 (MMC1), 16 × 16 KiB PRG, CHR-RAM,
battery.

| Check | Value |
| --- | --- |
| File size | 262,160 bytes (16-byte header + 262,144 PRG) |
| Full-file SHA-256 | `e59f95a80497779b861daa26e1b890929fd2f9939e78003898d9bff3ea3f6db2` |
| Headerless PRG SHA-256 | `84f5377980d2fd44d71faec42f858b1e83540c2f55aba9236c3279d6dde8592a` |

A file is accepted when either hash matches. The PRG check lets a clean dump
with a different header (for example iNES 1.0) work. ZIP archives are not
opened; the user extracts the `.nes` first.

## Error contract

`--check-rom` prints exactly one line to **stdout**. A normal launch prints
the same line to **stderr** before exiting or waiting:

```
MM_ROM_OK path=<path> sha256=<full-file sha256>
MM_ROM_ERROR code=<N> <human-readable reason>
```

| Exit | Meaning | Example reason |
| --- | --- | --- |
| 0 | ROM OK (or the game exited normally) | |
| 1 | runtime failure (SDL, display, audio device) | `error: cannot open a window: …` |
| 2 | no ROM given or found | `no Maniac Mansion (USA) ROM found` |
| 3 | the file cannot be opened or read | `cannot open ROM '/x.nes': No such file or directory` |
| 4 | not an NES ROM: no iNES header, a ZIP archive, truncated, wrong size | `'x.zip' is a ZIP archive; extract the .nes file first` |
| 5 | a valid NES file, but not the supported dump (other game, region, revision or a modified ROM) | `'x.nes' is not the supported Maniac Mansion (USA) dump (PRG SHA-256 0ebbdb27…, expected 84f53779…). Other regions and modified ROMs are not supported yet` |

Launchers can show the reason text as is. It is one line and names the file.
