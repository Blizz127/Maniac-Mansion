# Notice

## Ownership

Maniac Mansion, and all of its code, data, artwork, music, text and
trademarks, are owned by Lucasfilm Ltd. (Lucasfilm Games). The NES version
was published by Jaleco. Nintendo and NES are trademarks of Nintendo. This
project is not affiliated with, endorsed by or sponsored by Lucasfilm,
LucasArts, Disney, Jaleco or Nintendo.

## What this repository contains

- The source of a native runtime (`port/`): a 6502 CPU, PPU, APU and MMC1
  hardware core, an SDL2 host, and its tests, tools and documentation.
- Input traces (`port/traces/*.mmin`): button presses by frame number, used
  to verify the runtime against a reference emulator. They contain no game
  data.

The MIT license in [LICENSE](LICENSE) applies only to the original work in
this repository.

## What this repository does not contain

- Any game content: no ROM, no extracted PRG banks, graphics, music, SCUMM
  scripts, text or other assets, and no assembly generated from the game.
- Any saves, save states, screenshots or reference-emulator dumps of the game.

Every commit and release is checked by `port/tools/data-guard.py`. It
rejects ROM, save and image files and any file containing a 32-byte run of
the game's PRG data. Release packages carry `"contains_game_data": false` in
`build-info.json`.

## Third-party components

- **SDL2** (zlib license) is bundled in release packages as
  `lib/libSDL2-2.0.so.0`. Its notice is in `THIRD_PARTY.md` inside the
  package (`port/dist/THIRD_PARTY.md`).
- **font8x8** (public domain), by Daniel Hepper after Marcel Sondaar and the
  IBM public-domain VGA fonts, is compiled into the dev menu
  (`port/src/host/font8x8.h`).
- Mesen2 (GPL-3.0), FCEUX (GPL-2.0) and the NES test ROMs are used only as
  external tools during verification. None of their code or files are in
  this repository or its releases.
