#!/usr/bin/env python3
"""Refuse to commit or package retail game data.

  data-guard.py --rom rom/original.nes FILE...

Fails if any FILE is a ROM/save/dump by name or header, or contains any
32-byte run of the ROM's PRG data (catches extracted banks, tables and
graphics). Screenshots and frame dumps are rejected by extension.
"""
import argparse
import sys

BAD_EXT = (".nes", ".fds", ".unf", ".prg", ".chr", ".bin", ".sav", ".srm", ".raw", ".ram",
           ".zip", ".7z", ".mmo", ".mst", ".fcs", ".png", ".bmp", ".ppm", ".wav")
WIN = 32


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--rom", required=True)
    p.add_argument("files", nargs="+")
    a = p.parse_args()
    rom = open(a.rom, "rb").read()
    prg = rom[16:]
    windows = {prg[i:i + WIN] for i in range(0, len(prg) - WIN + 1)}
    # Runs of one repeated byte (padding) are not evidence of copying.
    windows = {w for w in windows if len(set(w)) > 2}
    bad = []
    for f in a.files:
        low = f.lower()
        if low.endswith(BAD_EXT):
            bad.append(f"{f}: data file type")
            continue
        try:
            d = open(f, "rb").read()
        except OSError as e:
            bad.append(f"{f}: {e}")
            continue
        if d[:4] == b"NES\x1a":
            bad.append(f"{f}: iNES header")
            continue
        for i in range(0, max(0, len(d) - WIN + 1)):
            if d[i:i + WIN] in windows:
                bad.append(f"{f}: contains ROM bytes at offset {i}")
                break
    for b in bad:
        print("data-guard: " + b, file=sys.stderr)
    print(f"data-guard: {len(a.files)} files checked, {len(bad)} rejected")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
