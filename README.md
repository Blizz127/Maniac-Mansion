# Maniac Mansion (NES) port

A native Linux runtime for **Maniac Mansion** on the NES (USA). It runs your
own ROM on a cycle-accurate recreation of the console's hardware, verified
frame by frame against the Mesen2 emulator. Over time, it will grow into a
decompilation-based port.

> **You need your own ROM.** This repository and its releases contain no game
> data: no ROM, no extracted banks, graphics, music, scripts or text. The
> port reads everything from your own copy of the game at run time and
> checks it by hash. See [NOTICE.md](NOTICE.md).
>
> **Just want to play?** Jump to [How to play](#how-to-play).

## About this project

This is a passion project. I'm working hard on it, but it's made for fun and
for everyone's enjoyment. It's free, non-commercial, and made by a fan. If
you enjoy it, that's the whole point.

## How to play

### 1. Download

From the [Releases page](https://github.com/Blizz127/Maniac-Mansion/releases),
download **`maniac-mansion-port-<tag>-linux-x86_64.tar.gz`**, plus
`SHA256SUMS` if you want to check it (`sha256sum -c SHA256SUMS`). It runs on
x86_64 Linux with glibc 2.34 or newer: SteamOS, Bazzite, and current
desktop distributions. SDL2 is included.

### 2. Install

Unpack it anywhere, for example into `~/Games`:

```sh
mkdir -p ~/Games
tar xzf ~/Downloads/maniac-mansion-port-*-linux-x86_64.tar.gz -C ~/Games
```

### 3. Your ROM

You need **Maniac Mansion (USA)** for the NES, as an `.nes` file: the
No-Intro dump, 262,160 bytes. Extract it first if it's in a `.zip`. A clean
dump with a different iNES header also works. Other regions and modified
ROMs are not supported yet.

| Check | Value |
| --- | --- |
| SHA-256 (whole file) | `e59f95a80497779b861daa26e1b890929fd2f9939e78003898d9bff3ea3f6db2` |
| SHA-256 (PRG, without the 16-byte header) | `84f5377980d2fd44d71faec42f858b1e83540c2f55aba9236c3279d6dde8592a` |

### 4. Run it

```sh
~/Games/maniac-mansion-port-*/launch.sh "/path/to/Maniac Mansion (USA).nes"
```

The ROM is remembered, so afterwards `launch.sh` alone is enough. You can
also set `MM_ROM`, put `rom = …` under `[game]` in
`~/.config/maniac-mansion-port/config.ini`, or drop the `.nes` file on the
window when asked. `launch.sh --check-rom FILE` only checks a ROM and prints
the result. On the Steam Deck or in Bazzite's Game Mode, add `launch.sh` as
a non-Steam game.

| | Keyboard | Controller |
| --- | --- | --- |
| D-pad | arrows | D-pad / left stick |
| A / B | X / Z | A / B (or X) |
| Start / Select | Enter / Right Shift | Start (Menu) / Back (View) |
| Fast-forward | F6 on/off, Backspace hold | L3 on/off, R3 hold |
| Screenshot, fullscreen, quit | F10, Alt+Enter, Esc | |

Battery saves are kept in `~/.local/share/maniac-mansion-port/saves/`. The
log is `~/.local/state/maniac-mansion-port/port.log`. All options, with
their defaults, are in `config.example.ini`. The defaults are the original
game on an NTSC console: 8:7 pixels at an integer scale, and nothing
enhanced.

An optional **dev menu** (fast-forward speed, screenshots, quick save and
load, display options) is off by default. Start with `MM_DEV_MENU=1`, then
press **F8**, or **Back+Start** on the same controller.

## Status

This is an alpha. The whole game is expected to run, because the runtime
recreates the hardware rather than the game. Only the parts covered by the
verification traces have been checked, though. Please report anything that
looks or sounds wrong.

- **Stage 1 (this release):** a native 6502, MMC1, PPU and APU runtime with
  an SDL2 host. It is verified against Mesen2 on scripted input traces: the
  power-on, title and intro frames are identical, and on an 8,936-frame
  gameplay trace, CPU memory at every frame, every sound-register write and
  the video memory are identical. The standard CPU, PPU and APU test ROMs
  pass. Details are in [docs/port.md](docs/port.md).
- **Stage 2 (planned):** statically recompiled C of the game's own routines,
  as the decompilation reviews them, co-simulated against Stage 1.

## Building from source

```sh
cmake -S port -B build/port
cmake --build build/port -j
build/port/maniac-mansion-port --rom "/path/to/Maniac Mansion (USA).nes"
```

This needs CMake, a C11 compiler and the SDL2 development package. Release
tarballs are built in a container with `port/tools/package-container.sh`
(see [docs/port-package.md](docs/port-package.md)). `port/tests/gate.sh` runs
the full verification gate. Test ROMs and reference-emulator output stay in
the ignored `build/` directory.

## Credits

- **Maniac Mansion** was created by Ron Gilbert and Gary Winnick at Lucasfilm
  Games. The NES version was published by Jaleco.
- [Mesen2](https://github.com/SourMesen/Mesen2) by Sour, the reference
  emulator for verification, and [FCEUX](https://fceux.com), the
  cross-check. Neither is included; both are used only for testing.
- The NESdev community's hardware documentation, and test ROMs by blargg and
  kevtris (nestest), used only for testing and not included.
- [SDL2](https://libsdl.org) (zlib license), bundled in releases, and
  [font8x8](https://github.com/dhepper/font8x8) (public domain), used for the
  dev menu text.

See [NOTICE.md](NOTICE.md) for ownership and licensing.
